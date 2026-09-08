#include "autosanitize.h"
#include "gamemanager.h"
#include "spellbook.h"
#include "ui/settingssanitize.h"
#include "ui/settingsdialog.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QFile>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class TestAutoSanitize : public QObject
{
	Q_OBJECT

	static void version( int bsVersion, int userVersion = 12 )
	{
		QSettings settings;
		settings.setValue( "Settings/Nif/Startup Defaults/Version", "20.2.0.7" );
		settings.setValue( "Settings/Nif/Startup Defaults/User Version", userVersion );
		settings.setValue( "Settings/Nif/Startup Defaults/User Version 2", bsVersion );
	}

	static SpellPtr spell( const char * name )
	{
		return SpellBook::lookup( QString( "Sanitize/" ) + name );
	}

private slots:
	void initTestCase()
	{
		QVERIFY( NifModel::loadXML() );
	}

	void init()
	{
		QFile::remove( AutoSanitizePolicy::configPath() );
		version( 170 );
	}

	void cameraNames_data()
	{
		QTest::addColumn<QString>( "name" );
		QTest::addColumn<int>( "rawIndex" );
		QTest::newRow( "empty-string" ) << QString() << 0;
		QTest::newRow( "unset" ) << QString() << -1;
		QTest::newRow( "invalid-index" ) << QString() << 12345;
		QTest::newRow( "existing-name" ) << QString( "Camera" ) << -2;
		QTest::newRow( "bsx-name" ) << QString( "BSX" ) << -2;
	}

	void suppliedCameraFixture()
	{
		const QString source = qEnvironmentVariable( "NIFSKOPE_CAMERA_FIXTURE" );
		if ( source.isEmpty() )
			QSKIP( "Set NIFSKOPE_CAMERA_FIXTURE to the local Starfield skeleton.nif fixture." );
		QFile fixture( source );
		QVERIFY( fixture.open( QIODevice::ReadOnly ) );
		const QByteArray original = fixture.readAll();
		fixture.close();
		qInfo() << "Fixture SHA256:" << QCryptographicHash::hash( original, QCryptographicHash::Sha256 ).toHex();
		QTemporaryDir copies;
		QVERIFY( copies.isValid() );
		const QString copy = copies.filePath( "skeleton-test.nif" );
		QVERIFY( QFile::copy( source, copy ) );
		NifModel nif;
		QVERIFY( nif.loadFromFile( copy ) );
		QCOMPARE( Game::GameManager::get_game( &nif ), Game::STARFIELD );
		qInfo() << "Fixture Bethesda stream version:" << nif.getBSVersion();
		const int blockCount = nif.getBlockCount();
		QVERIFY( nif.blockInherits( nif.getBlockIndex( 252 ), "NiCamera" ) );
		QCOMPARE( nif.get<QString>( nif.getBlockIndex( 252 ), "Name" ), QString() );
		const int nameIndex = nif.get<int>( nif.getBlockIndex( 252 ), "Name" );
		qInfo() << "Before: blocks=" << blockCount << "block=252 type=NiCamera name=empty raw-index=" << nameIndex;
		for ( int pass = 1; pass <= 3; pass++ ) {
			QVERIFY( nif.sanitizeBeforeSave() );
			QCOMPARE( nif.get<int>( nif.getBlockIndex( 252 ), "Name" ), nameIndex );
			QVERIFY( nif.saveToFile( copy ) );
			QVERIFY( nif.loadFromFile( copy ) );
			QCOMPARE( nif.getBlockCount(), blockCount );
			QVERIFY( nif.blockInherits( nif.getBlockIndex( 252 ), "NiCamera" ) );
			QCOMPARE( nif.get<QString>( nif.getBlockIndex( 252 ), "Name" ), QString() );
			QCOMPARE( nif.get<int>( nif.getBlockIndex( 252 ), "Name" ), nameIndex );
			qInfo() << "Auto-sanitize/save/reload pass" << pass << ": camera name still empty; raw index unchanged";
		}
		auto names = spell( "Fix Invalid Block Names" );
		QVERIFY( names );
		names->cast( &nif, {} );
		QVERIFY( nif.saveToFile( copy ) );
		QVERIFY( nif.loadFromFile( copy ) );
		QCOMPARE( nif.get<QString>( nif.getBlockIndex( 252 ), "Name" ), QString() );
		QCOMPARE( nif.get<int>( nif.getBlockIndex( 252 ), "Name" ), nameIndex );
		qInfo() << "Manual name repair/save/reload: camera name still empty; raw index unchanged";
		QVERIFY( QFile::remove( copy ) );
		QVERIFY( !QFile::exists( copy ) );
		QVERIFY( fixture.open( QIODevice::ReadOnly ) );
		QCOMPARE( fixture.readAll(), original );
		qInfo() << "Disposable copy deleted; fixture bytes unchanged";
	}

	void cameraNames()
	{
		QFETCH( QString, name );
		QFETCH( int, rawIndex );
		NifModel nif;
		QCOMPARE( nif.getBSVersion(), quint32(170) );
		if ( rawIndex == 0 ) {
			auto header = nif.getHeaderIndex();
			nif.set<int>( header, "Num Strings", 1 );
			nif.updateArraySize( header, "Strings" );
			nif.setArray<QString>( header, "Strings", { "" } );
		}
		for ( int i = 0; i < 2; i++ ) {
			auto camera = nif.insertNiBlock( "NiCamera" );
			QVERIFY( camera.isValid() );
			QVERIFY( nif.assignString( camera, "Name", name ) );
			if ( rawIndex != -2 )
				nif.set<int>( camera, "Name", rawIndex );
		}
		int originalIndex = nif.get<int>( nif.getBlockIndex( 1 ), "Name" );
		// Ordinary duplicate names must still be repaired.
		for ( int i = 0; i < 2; i++ ) {
			auto node = nif.insertNiBlock( "NiNode" );
			QVERIFY( nif.assignString( node, "Name", QStringLiteral("Duplicate") ) );
		}
		QVERIFY( nif.sanitizeBeforeSave() );
		for ( int i = 0; i < 2; i++ )
			QCOMPARE( nif.get<int>( nif.getBlockIndex( i ), "Name" ), originalIndex );
		QVERIFY( nif.get<QString>( nif.getBlockIndex( 2 ), "Name" ) != nif.get<QString>( nif.getBlockIndex( 3 ), "Name" ) );
		auto names = spell( "Fix Invalid Block Names" );
		QVERIFY( names );
		names->cast( &nif, {} );
		QCOMPARE( nif.get<int>( nif.getBlockIndex( 1 ), "Name" ), originalIndex );
		QBuffer buffer;
		QVERIFY( buffer.open( QIODevice::ReadWrite ) );
		QVERIFY( nif.save( buffer ) );
		QVERIFY( buffer.seek( 0 ) );
		NifModel reloaded;
		QVERIFY( reloaded.load( buffer ) );
		QCOMPARE( reloaded.get<int>( reloaded.getBlockIndex( 1 ), "Name" ), originalIndex );
		QVERIFY( reloaded.sanitizeBeforeSave() );
		QCOMPARE( reloaded.get<int>( reloaded.getBlockIndex( 1 ), "Name" ), originalIndex );
	}

	void olderGameCamera()
	{
		version( 100 );
		NifModel nif;
		for ( int i = 0; i < 2; i++ )
			nif.assignString( nif.insertNiBlock( "NiCamera" ), "Name", QString() );
		QVERIFY( !AutoSanitizePolicy::protectsCamera( &nif, nif.getBlockIndex( 1 ) ) );
		spell( "Fix Invalid Block Names" )->cast( &nif, {} );
		QVERIFY( !nif.get<QString>( nif.getBlockIndex( 1 ), "Name" ).isEmpty() );
	}

	void matchingAndPersistence()
	{
		QString path = AutoSanitizePolicy::configPath();
		{
			QSettings settings( path, QSettings::IniFormat );
			settings.setValue( "future-operation/Value", "preserve me" );
		}
		AutoSanitizePolicy policy;
		policy.rules[AutoSanitizePolicy::ruleKey( AutoSanitizePolicy::FixNames, "starfield", false )] = QStringList{ "NiNode" };
		policy.rules[AutoSanitizePolicy::ruleKey( AutoSanitizePolicy::CollapseLinks, "", true )] = QStringList{ "NiNode" };
		QString error;
		QVERIFY2( policy.save( path, error ), qPrintable(error) );
		AutoSanitizePolicy loaded;
		loaded.load( path );
		QCOMPARE( loaded.rules, policy.rules );
		QCOMPARE( loaded.diagnostics.size(), 1 );
		NifModel nif;
		auto node = nif.insertNiBlock( "NiNode" );
		auto derived = nif.insertNiBlock( "BSFadeNode" );
		QVERIFY( loaded.excludes( AutoSanitizePolicy::FixNames, &nif, node ) );
		QVERIFY( !loaded.excludes( AutoSanitizePolicy::FixNames, &nif, derived ) );
		QVERIFY( loaded.excludes( AutoSanitizePolicy::CollapseLinks, &nif, derived ) );
		QVERIFY( !loaded.excludes( AutoSanitizePolicy::ReorderLinks, &nif, node ) );
		version( 100 );
		NifModel older;
		auto olderNode = older.insertNiBlock( "NiNode" );
		QVERIFY( !loaded.excludes( AutoSanitizePolicy::FixNames, &older, olderNode ) );
		QVERIFY( loaded.excludes( AutoSanitizePolicy::CollapseLinks, &older, olderNode ) );
		loaded.rules.clear();
		QVERIFY( loaded.save( path, error ) );
		QSettings settings( path, QSettings::IniFormat );
		QCOMPARE( settings.value( "future-operation/Value" ).toString(), QString( "preserve me" ) );
		QCOMPARE( settings.allKeys().size(), 1 );
	}

	void pipelineAndManualIsolation()
	{
		AutoSanitizePolicy policy;
		policy.rules[AutoSanitizePolicy::ruleKey( AutoSanitizePolicy::FixNames, "", false )] = QStringList{ "NiNode" };
		QString error;
		QVERIFY( policy.save( AutoSanitizePolicy::configPath(), error ) );
		NifModel nif;
		for ( int i = 0; i < 2; i++ )
			nif.assignString( nif.insertNiBlock( "NiNode" ), "Name", QStringLiteral("Same") );
		SpellBook::sanitize( &nif );
		QCOMPARE( nif.get<QString>( nif.getBlockIndex( 1 ), "Name" ), QString( "Same" ) );
		spell( "Fix Invalid Block Names" )->cast( &nif, {} );
		QVERIFY( nif.get<QString>( nif.getBlockIndex( 1 ), "Name" ) != "Same" );
	}

	void linkArrayIsolation()
	{
		NifModel nif;
		auto node = nif.insertNiBlock( "NiNode" );
		nif.set<int>( node, "Num Children", 2 );
		nif.updateArraySize( node, "Children" );
		nif.setLinkArray( node, "Children", { -1, -1 } );
		AutoSanitizePolicy policy;
		policy.rules[AutoSanitizePolicy::ruleKey( AutoSanitizePolicy::CollapseLinks, "", false )] = QStringList{ "NiNode" };
		spell( "Collapse Link Arrays" )->castSanitize( &nif, policy );
		QCOMPARE( nif.get<int>( node, "Num Children" ), 2 );
		// Reordering also collapses empty children, so its exclusion is independent.
		spell( "Reorder Link Arrays" )->castSanitize( &nif, policy );
		QCOMPARE( nif.get<int>( node, "Num Children" ), 0 );
	}

	void otherModifyingOperations()
	{
		version( 34, 11 );
		NifModel nif;
		auto texture = nif.insertNiBlock( "NiSourceTexture" );
		nif.set<quint8>( texture, "Use External", 1 );
		QVERIFY( nif.assignString( texture, "File Name", QStringLiteral("textures/test.dds") ) );
		auto data = nif.insertNiBlock( "NiTriShapeData" );
		nif.set<int>( data, "Group ID", 42 );
		auto node = nif.insertNiBlock( "NiNode" );
		nif.set<int>( node, "Num Children", 1 );
		nif.updateArraySize( node, "Children" );
		nif.setLinkArray( node, "Children", { -1 } );
		AutoSanitizePolicy policy;
		policy.rules[AutoSanitizePolicy::ruleKey( AutoSanitizePolicy::AdjustTextures, "", false )] = QStringList{ "NiSourceTexture" };
		policy.rules[AutoSanitizePolicy::ruleKey( AutoSanitizePolicy::FixGeometryData, "", true )] = QStringList{ "NiGeometryData" };
		policy.rules[AutoSanitizePolicy::ruleKey( AutoSanitizePolicy::ReorderLinks, "", false )] = QStringList{ "NiNode" };
		for ( const char * name : { "Adjust Texture Sources", "Fix Geometry Data Names", "Reorder Link Arrays" } ) {
			auto operation = spell( name );
			QVERIFY( operation && operation->isApplicable( &nif, {} ) );
			operation->castSanitize( &nif, policy );
		}
		QCOMPARE( nif.get<QString>( texture, "File Name" ), QString( "textures/test.dds" ) );
		QCOMPARE( nif.get<int>( data, "Group ID" ), 42 );
		QCOMPARE( nif.get<int>( node, "Num Children" ), 1 );
		policy.rules.clear();
		for ( const char * name : { "Adjust Texture Sources", "Fix Geometry Data Names", "Reorder Link Arrays" } )
			spell( name )->castSanitize( &nif, policy );
		QCOMPARE( nif.get<QString>( texture, "File Name" ), QString( "textures\\test.dds" ) );
		QCOMPARE( nif.get<int>( data, "Group ID" ), 0 );
		QCOMPARE( nif.get<int>( node, "Num Children" ), 0 );
	}

	void unknownEntries()
	{
		{
			QSettings settings( AutoSanitizePolicy::configPath(), QSettings::IniFormat );
			settings.setValue( "fix-invalid-block-names/BlockTypes", QStringList{ "NiNode", "NoSuchBlock", "NiNode" } );
			settings.setValue( "fix-invalid-block-names/no-such-game/BlockTypes", "NiCamera" );
		}
		AutoSanitizePolicy policy;
		policy.load( AutoSanitizePolicy::configPath() );
		QCOMPARE( policy.diagnostics.size(), 2 );
		QCOMPARE( policy.rules.value( "fix-invalid-block-names/BlockTypes" ).size(), 2 );
		NifModel nif;
		QVERIFY( policy.excludes( AutoSanitizePolicy::FixNames, &nif, nif.insertNiBlock( "NiNode" ) ) );
		QString error;
		QVERIFY( policy.save( AutoSanitizePolicy::configPath(), error ) );
		policy.load( AutoSanitizePolicy::configPath() );
		QCOMPARE( policy.diagnostics.size(), 2 );
	}

	void malformedConfiguration()
	{
		QString path = AutoSanitizePolicy::configPath();
		QDir().mkpath( QFileInfo(path).absolutePath() );
		QFile file( path );
		QVERIFY( file.open( QIODevice::WriteOnly ) );
		file.write( "[broken\nBlockTypes=NiNode\n" );
		file.close();
		AutoSanitizePolicy policy;
		policy.load( path );
		QVERIFY( !policy.diagnostics.isEmpty() );
		QVERIFY( policy.rules.isEmpty() );
		NifModel nif;
		QVERIFY( policy.excludes( AutoSanitizePolicy::FixNames, &nif, nif.insertNiBlock( "NiCamera" ) ) );
		QString error;
		QVERIFY( !policy.save( path, error ) );
		QVERIFY( !error.isEmpty() );
	}

	void editorRoundTrip()
	{
		SettingsSanitize editor;
		auto type = editor.findChild<QComboBox *>( "sanitizeBlockType" );
		auto game = editor.findChild<QComboBox *>( "sanitizeScope" );
		auto derived = editor.findChild<QCheckBox *>( "sanitizeIncludeDerived" );
		auto list = editor.findChild<QListWidget *>( "sanitizeExclusions" );
		auto add = editor.findChild<QPushButton *>( "sanitizeAdd" );
		QVERIFY( type && game && derived && list && add );
		game->setCurrentIndex( game->findData( "starfield" ) );
		type->setEditText( "NotARealBlock" );
		add->click();
		QCOMPARE( list->count(), 0 );
		type->setCurrentText( "NiAVObject" );
		derived->setChecked( true );
		add->click();
		add->click();
		QCOMPARE( list->count(), 1 );
		QVERIFY( editor.saveChanges() );
		AutoSanitizePolicy policy;
		policy.load( AutoSanitizePolicy::configPath() );
		QCOMPARE( policy.rules.value( AutoSanitizePolicy::ruleKey( AutoSanitizePolicy::FixNames, "starfield", true ) ), QStringList{ "NiAVObject" } );
		editor.setDefault();
		QCOMPARE( list->count(), 0 );
		// Cancel/reload discards unapplied edits.
		editor.read();
		QCOMPARE( list->count(), 1 );
		list->item( 0 )->setSelected( true );
		editor.findChild<QPushButton *>( "sanitizeRemove" )->click();
		QVERIFY( editor.saveChanges() );
		policy.load( AutoSanitizePolicy::configPath() );
		QVERIFY( policy.rules.isEmpty() );
	}

	void dialogKeepsUnappliedChangesOnFailure()
	{
		QSettings().setValue( "Settings/Version", 1.0 );
		SettingsDialog dialog;
		dialog.show();
		auto editor = dialog.findChild<SettingsSanitize *>();
		QVERIFY( editor );
		dialog.categories->setCurrentRow( dialog.content->indexOf( editor ) );
		auto type = editor->findChild<QComboBox *>( "sanitizeBlockType" );
		type->setCurrentText( "NiNode" );
		editor->findChild<QPushButton *>( "sanitizeAdd" )->click();
		QString screenshot = qEnvironmentVariable( "NIFSKOPE_TEST_SCREENSHOT" );
		if ( !screenshot.isEmpty() ) {
			QApplication::processEvents();
			QVERIFY( dialog.grab().save( screenshot ) );
		}
		{
			QSettings external( AutoSanitizePolicy::configPath(), QSettings::IniFormat );
			external.setValue( "external-edit/Value", "keep" );
		}
		// Close the expected conflict warning; Save must leave the settings dialog open.
		QTimer::singleShot( 0, []() {
			if ( auto message = qobject_cast<QMessageBox *>( QApplication::activeModalWidget() ) )
				message->accept();
		} );
		dialog.save();
		QVERIFY( dialog.isVisible() );
		QVERIFY( dialog.findChild<QDialogButtonBox *>()->button( QDialogButtonBox::Apply )->isEnabled() );
		QCOMPARE( editor->findChild<QListWidget *>( "sanitizeExclusions" )->count(), 1 );
		dialog.cancel();
		AutoSanitizePolicy policy;
		policy.load( AutoSanitizePolicy::configPath() );
		QVERIFY( policy.rules.isEmpty() );
	}
};

int main( int argc, char ** argv )
{
	QTemporaryDir config;
	if ( !config.isValid() )
		return 1;
	qputenv( "XDG_CONFIG_HOME", config.path().toUtf8() );
	QStandardPaths::setTestModeEnabled( true );
	QApplication app( argc, argv );
	QCoreApplication::setOrganizationName( "NifSkopeTests" );
	QCoreApplication::setApplicationName( "AutoSanitize" );
	QSettings::setDefaultFormat( QSettings::IniFormat );
	QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, config.path() );
	TestAutoSanitize test;
	int result = QTest::qExec( &test, argc, argv );
	QFile::remove( AutoSanitizePolicy::configPath() );
	return result;
}

#include "tst_autosanitize.moc"
