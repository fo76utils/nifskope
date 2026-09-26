#include "settingssanitize.h"
#include "settingsdialog.h"

#include "model/nifmodel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

SettingsSanitize::SettingsSanitize( QWidget * parent ) : SettingsPane( parent )
{
	SettingsDialog::registerPage( parent, tr( "Auto-Sanitize" ) );
	auto layout = new QVBoxLayout( this );
	auto explanation = new QLabel( tr( "Exclude block types from individual auto-sanitize operations. "
		"All-games rules and game-specific rules are combined. Validation still runs." ), this );
	explanation->setWordWrap( true );
	layout->addWidget( explanation );
	auto form = new QFormLayout;
	operation = new QComboBox( this );
	operation->setObjectName( "sanitizeOperation" );
	for ( int op = 0; op < AutoSanitizePolicy::OperationCount; op++ )
		operation->addItem( AutoSanitizePolicy::operationName( AutoSanitizePolicy::Operation(op) ) );
	scope = new QComboBox( this );
	scope->setObjectName( "sanitizeScope" );
	for ( const QString & id : AutoSanitizePolicy::scopes() )
		scope->addItem( AutoSanitizePolicy::scopeName( id ), id );
	form->addRow( tr( "Operation" ), operation );
	form->addRow( tr( "Game" ), scope );
	layout->addLayout( form );
	exclusions = new QListWidget( this );
	exclusions->setObjectName( "sanitizeExclusions" );
	exclusions->setAccessibleName( tr( "Excluded block types" ) );
	exclusions->setSelectionMode( QAbstractItemView::ExtendedSelection );
	layout->addWidget( exclusions );
	auto addRow = new QHBoxLayout;
	blockType = new QComboBox( this );
	blockType->setObjectName( "sanitizeBlockType" );
	blockType->setAccessibleName( tr( "Block type" ) );
	blockType->setEditable( true );
	blockType->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
	blockType->setMinimumContentsLength( 20 );
	blockType->setInsertPolicy( QComboBox::NoInsert );
	QStringList types = NifModel::allNiBlocks( true );
	types.sort();
	blockType->addItems( types );
	includeDerived = new QCheckBox( tr( "Include derived types" ), this );
	includeDerived->setObjectName( "sanitizeIncludeDerived" );
	auto add = new QPushButton( tr( "Add" ), this );
	add->setObjectName( "sanitizeAdd" );
	addRow->addWidget( blockType, 1 );
	addRow->addWidget( includeDerived );
	addRow->addWidget( add );
	layout->addLayout( addRow );
	auto buttons = new QHBoxLayout;
	auto remove = new QPushButton( tr( "Remove Selected" ), this );
	remove->setObjectName( "sanitizeRemove" );
	auto reset = new QPushButton( tr( "Reset Custom Exclusions" ), this );
	reset->setObjectName( "sanitizeReset" );
	auto reload = new QPushButton( tr( "Reload File" ), this );
	buttons->addWidget( remove );
	buttons->addWidget( reset );
	buttons->addStretch();
	buttons->addWidget( reload );
	layout->addLayout( buttons );
	auto protection = new QLabel( tr( "Built-in protection: Starfield NiCamera blocks (including derived types) "
		"are excluded from Fix Invalid Block Names, including manual invocation. This rule cannot be removed. "
		"Existing names are preserved, not cleared." ), this );
	protection->setWordWrap( true );
	layout->addWidget( protection );
	configLocation = new QLabel( this );
	configLocation->setWordWrap( true );
	configLocation->setTextFormat( Qt::PlainText );
	configLocation->setTextInteractionFlags( Qt::TextSelectableByMouse );
	layout->addWidget( configLocation );
	diagnostics = new QPlainTextEdit( this );
	diagnostics->setAccessibleName( tr( "Configuration errors" ) );
	diagnostics->setReadOnly( true );
	diagnostics->setMaximumHeight( 80 );
	diagnostics->setPlaceholderText( tr( "No configuration errors." ) );
	layout->addWidget( diagnostics );
	connect( operation, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, &SettingsSanitize::refreshRules );
	connect( scope, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, &SettingsSanitize::refreshRules );
	connect( add, &QPushButton::clicked, this, [this]() {
		QString type = blockType->currentText().trimmed();
		if ( blockType->findText( type, Qt::MatchExactly ) < 0 ) {
			diagnostics->setPlainText( tr( "Select a known block type. Block type names are case-sensitive." ) );
			return;
		}
		if ( NifModel::isAncestor( type ) && !includeDerived->isChecked() ) {
			diagnostics->setPlainText( tr( "This is an abstract block type. Enable Include derived types to match its subclasses." ) );
			return;
		}
		QString key = currentKey( includeDerived->isChecked() );
		diagnostics->setPlainText( policy.diagnostics.join( '\n' ) );
		if ( !policy.rules[key].contains( type ) ) {
			policy.rules[key].append( type );
			modifyPane();
			refreshRules();
		}
	} );
	connect( remove, &QPushButton::clicked, this, [this]() {
		const auto selected = exclusions->selectedItems();
		for ( auto item : selected )
			policy.rules[currentKey( item->data( Qt::UserRole + 1 ).toBool() )].removeAll( item->data( Qt::UserRole ).toString() );
		if ( !selected.isEmpty() ) {
			modifyPane();
			refreshRules();
		}
	} );
	connect( reset, &QPushButton::clicked, this, &SettingsSanitize::setDefault );
	connect( reload, &QPushButton::clicked, this, [this]() {
		if ( !isModified() || QMessageBox::question( this, tr( "Reload Auto-Sanitize Configuration" ),
			tr( "Discard unapplied exclusion changes and reload the file?" ) ) == QMessageBox::Yes )
			read();
	} );
	read();
}

QString SettingsSanitize::currentKey( bool derived ) const
{
	return AutoSanitizePolicy::ruleKey( AutoSanitizePolicy::Operation(operation->currentIndex()), scope->currentData().toString(), derived );
}

void SettingsSanitize::refreshRules()
{
	exclusions->clear();
	for ( bool derived : { false, true } ) {
		for ( const QString & type : policy.rules.value( currentKey( derived ) ) ) {
			auto item = new QListWidgetItem( derived ? tr( "%1 (including derived types)" ).arg( type ) : type, exclusions );
			item->setData( Qt::UserRole, type );
			item->setData( Qt::UserRole + 1, derived );
		}
	}
}

void SettingsSanitize::read()
{
	path = AutoSanitizePolicy::configPath();
	policy.load( path );
	QFile file( path );
	loadedContents = file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray();
	configLocation->setText( tr( "Configuration: %1" ).arg( path ) );
	diagnostics->setPlainText( policy.diagnostics.join( '\n' ) );
	refreshRules();
	setModified( false );
}

bool SettingsSanitize::saveChanges()
{
	if ( !isModified() )
		return true;
	QFile file( path );
	QByteArray currentContents = file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray();
	file.close();
	QString error;
	if ( path != AutoSanitizePolicy::configPath() || currentContents != loadedContents )
		error = tr( "The configuration file changed outside this editor. Reload it before saving." );
	else if ( policy.save( path, error ) ) {
		read();
		return true;
	}
	diagnostics->setPlainText( error );
	QMessageBox::warning( this, tr( "Auto-Sanitize Configuration" ), error );
	return false;
}

void SettingsSanitize::write()
{
	saveChanges();
}

void SettingsSanitize::setDefault()
{
	policy.rules.clear();
	refreshRules();
	modifyPane();
}
