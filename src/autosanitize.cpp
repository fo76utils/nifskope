#include "autosanitize.h"

#include "gamemanager.h"
#include "model/nifmodel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

QString AutoSanitizePolicy::operationId( Operation operation )
{
	static const char * ids[] = {
		"fix-invalid-block-names", "reorder-link-arrays", "collapse-link-arrays",
		"adjust-texture-sources", "fix-geometry-data-names"
	};
	return QString::fromLatin1( ids[operation] );
}

QString AutoSanitizePolicy::operationName( Operation operation )
{
	static const char * names[] = {
		QT_TRANSLATE_NOOP( "Spell", "Fix Invalid Block Names" ),
		QT_TRANSLATE_NOOP( "Spell", "Reorder Link Arrays" ),
		QT_TRANSLATE_NOOP( "Spell", "Collapse Link Arrays" ),
		QT_TRANSLATE_NOOP( "Spell", "Adjust Texture Sources" ),
		QT_TRANSLATE_NOOP( "Spell", "Fix Geometry Data Names" )
	};
	return QCoreApplication::translate( "Spell", names[operation] );
}

QStringList AutoSanitizePolicy::scopes()
{
	// Non-empty entries follow Game::GameMode order.
	return { "", "other", "morrowind", "oblivion", "fallout-3-nv", "skyrim",
		"skyrim-se", "fallout-4", "fallout-76", "starfield" };
}

QString AutoSanitizePolicy::scopeName( const QString & scope )
{
	int index = scopes().indexOf( scope );
	if ( index == 0 )
		return QCoreApplication::translate( "AutoSanitizePolicy", "All games" );
	return index > 0 ? Game::StringForMode( Game::GameMode(index - 1) ) : scope;
}

QString AutoSanitizePolicy::configPath()
{
	// Use a fixed application component so GUI and headless invocations share rules.
	QString userPath = QStandardPaths::writableLocation( QStandardPaths::GenericConfigLocation )
		+ "/NifTools/NifSkope/autosanitize.ini";
	QString portablePath = QCoreApplication::applicationDirPath() + "/autosanitize.ini";
	if ( QFileInfo::exists( userPath ) || !QFileInfo::exists( portablePath ) )
		return userPath;
	return portablePath;
}

QString AutoSanitizePolicy::ruleKey( Operation operation, const QString & scope, bool derived )
{
	return operationId( operation ) + "/" + (scope.isEmpty() ? QString() : scope + "/")
		+ (derived ? "DerivedBlockTypes" : "BlockTypes");
}

void AutoSanitizePolicy::load( const QString & path )
{
	rules.clear();
	diagnostics.clear();
	QSettings settings( path, QSettings::IniFormat );
	QStringList knownKeys;
	for ( int op = 0; op < OperationCount; op++ ) {
		for ( const QString & scope : scopes() ) {
			for ( bool derived : { false, true } )
				knownKeys.append( ruleKey( Operation(op), scope, derived ) );
		}
	}
	const QStringList knownTypes = NifModel::allNiBlocks( true );
	for ( const QString & key : settings.allKeys() ) {
		if ( !knownKeys.contains( key ) ) {
			diagnostics.append( QCoreApplication::translate( "AutoSanitizePolicy",
				"Unknown setting: %1 (preserved, ignored by auto-sanitize)." ).arg( key ) );
			continue;
		}
		QStringList types;
		for ( const QString & value : settings.value( key ).toStringList() ) {
			QString type = value.trimmed();
			if ( type.isEmpty() || types.contains( type ) )
				continue;
			types.append( type );
			if ( !knownTypes.contains( type ) ) {
				diagnostics.append( QCoreApplication::translate( "AutoSanitizePolicy",
					"Unknown block type in %1: %2 (ignored)." ).arg( key, type ) );
			}
		}
		rules.insert( key, types );
	}
	formatError = settings.status() != QSettings::NoError;
	if ( formatError ) {
		diagnostics.append( QCoreApplication::translate( "AutoSanitizePolicy",
			"Cannot read %1. Custom exclusions were not loaded; built-in protection remains active." ).arg( path ) );
		rules.clear();
	}
}

bool AutoSanitizePolicy::save( const QString & path, QString & error ) const
{
	error.clear();
	if ( formatError ) {
		error = QCoreApplication::translate( "AutoSanitizePolicy",
			"Correct the configuration file and reload it before saving." );
		return false;
	}
	if ( !QDir().mkpath( QFileInfo(path).absolutePath() ) ) {
		error = QCoreApplication::translate( "AutoSanitizePolicy", "Cannot create the configuration directory for %1." ).arg( path );
		return false;
	}
	QSettings settings( path, QSettings::IniFormat );
	// Read before writing, preserving unrelated keys and refusing malformed files.
	settings.allKeys();
	if ( settings.status() == QSettings::NoError ) {
		for ( int op = 0; op < OperationCount; op++ ) {
			for ( const QString & scope : scopes() ) {
				for ( bool derived : { false, true } ) {
					QString key = ruleKey( Operation(op), scope, derived );
					if ( rules.value( key ).isEmpty() )
						settings.remove( key );
					else
						settings.setValue( key, rules.value( key ) );
				}
			}
		}
		settings.sync();
	}
	if ( settings.status() != QSettings::NoError ) {
		error = QCoreApplication::translate( "AutoSanitizePolicy", "Could not save %1. Check its syntax and write permissions." ).arg( path );
		return false;
	}
	return true;
}

bool AutoSanitizePolicy::protectsCamera( const NifModel * nif, const QModelIndex & block )
{
	return nif && block.isValid() && Game::GameManager::get_game( nif ) == Game::STARFIELD
		&& nif->blockInherits( block, "NiCamera" );
}

bool AutoSanitizePolicy::excludes( Operation operation, const NifModel * nif, const QModelIndex & block ) const
{
	if ( !nif || !block.isValid() )
		return false;
	if ( operation == FixNames && protectsCamera( nif, block ) )
		return true;
	QString gameScope = scopes().value( int(Game::GameManager::get_game( nif )) + 1 );
	for ( const QString & scope : { QString(), gameScope } ) {
		for ( const QString & type : rules.value( ruleKey( operation, scope, false ) ) ) {
			if ( nif->isNiBlock( block, type ) )
				return true;
		}
		for ( const QString & type : rules.value( ruleKey( operation, scope, true ) ) ) {
			if ( nif->blockInherits( block, type ) )
				return true;
		}
	}
	return false;
}
