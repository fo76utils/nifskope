#include "spellbook.h"

#include <QDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

#include "ui/widgets/filebrowser.h"
#include "libfo76utils/src/common.hpp"
#include "libfo76utils/src/material.hpp"
#include "model/nifmodel.h"

// Brief description is deliberately not autolinked to class Spell
/*! \file headerstring.cpp
 * \brief Header string editing spells (spEditStringIndex)
 *
 * All classes here inherit from the Spell class.
 */

/* XPM */
static char const * txt_xpm[] = {
	"32 32 36 1",
	"   c None",
	".	c #FFFFFF", "+	c #000000", "@	c #BDBDBD", "#	c #717171", "$	c #252525",
	"%	c #4F4F4F", "&	c #A9A9A9", "*	c #A8A8A8", "=	c #555555", "-	c #EAEAEA",
	";	c #151515", ">	c #131313", ",	c #D0D0D0", "'	c #AAAAAA", ")	c #080808",
	"!	c #ABABAB", "~	c #565656", "{	c #D1D1D1", "]	c #4D4D4D", "^	c #4E4E4E",
	"/	c #FDFDFD", "(	c #A4A4A4", "_	c #0A0A0A", ":	c #A5A5A5", "<	c #050505",
	"[	c #C4C4C4", "}	c #E9E9E9", "|	c #D5D5D5", "1	c #141414", "2	c #3E3E3E",
	"3	c #DDDDDD", "4	c #424242", "5	c #070707", "6	c #040404", "7	c #202020",
	"                                ",
	"                                ",
	"                                ",
	"                                ",
	"                                ",
	"                                ",
	"                                ",
	"                                ",
	"                                ",
	" ...........          ....      ",
	" .+++++++++.         .@#$.      ",
	" .+++++++++.         .+++.      ",
	" ....+++..............+++...    ",
	"    .+++.   %++&.*++=++++++.    ",
	"    .+++.  .-;+>,>+;-++++++.    ",
	"    .+++.   .'++)++!..+++...    ",
	"    .+++.    .=+++~. .+++.      ",
	"    .+++.    .{+++{. .+++.      ",
	"    .+++.    .]+++^. .+++/      ",
	"    .+++.   .(++_++:..<++[..    ",
	"    .+++.  .}>+;|;+1}.2++++.    ",
	"    .+++.   ^++'.'++%.34567.    ",
	"    .....  .................    ",
	"                                ",
	"                                ",
	"                                ",
	"                                ",
	"                                ",
	"                                ",
	"                                ",
	"                                ",
	"                                "
};

static QIconPtr txt_xpm_icon = nullptr;

//! Edit the index of a header string
class spEditStringIndex final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Edit String Index" ); }
	QString page() const override final { return Spell::tr( "" ); }
	QIcon icon() const override final
	{
		if ( !txt_xpm_icon )
			txt_xpm_icon = QIconPtr( new QIcon(QPixmap( txt_xpm )) );

		return *txt_xpm_icon;
	}
	bool constant() const override final { return true; }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		const NifItem * item = nif->getItem( index );
		if ( item ) {
			auto vt = item->valueType();
			if ( vt == NifValue::tStringIndex )
				return true;
			if ( nif->checkVersion( 0x14010003, 0 ) && ( vt == NifValue::tString || vt == NifValue::tFilePath ) )
				return true;
		}

		return false;
	}

	static QString browseMaterial( const NifModel * nif, const QString & matPath );
	void browseMaterial( QLineEdit * le, const NifModel * nif );

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		int offset = nif->get<int>( index );
		QStringList strings;
		QString string;

		if ( nif->getValue( index ).type() != NifValue::tStringIndex || !nif->checkVersion( 0x14010003, 0 ) )
			return index;

		QModelIndex header = nif->getHeaderIndex();
		QVector<QString> stringVector = nif->getArray<QString>( header, "Strings" );
		strings = stringVector.toList();

		if ( offset >= 0 && offset < stringVector.size() )
			string = stringVector.at( offset );

		QDialog dlg;

		QLabel * lb = new QLabel( &dlg );
		lb->setText( Spell::tr( "Select a string or enter a new one" ) );

		QListWidget * lw = new QListWidget( &dlg );
		lw->addItems( strings );

		QLineEdit * le = new QLineEdit( &dlg );
		le->setText( string );
		le->setFocus();

		QObject::connect( lw, &QListWidget::currentTextChanged, le, &QLineEdit::setText );
		QObject::connect( lw, &QListWidget::itemActivated, &dlg, &QDialog::accept );
		QObject::connect( le, &QLineEdit::returnPressed, &dlg, &QDialog::accept );

		QPushButton * bo = new QPushButton( Spell::tr( "Ok" ), &dlg );
		QObject::connect( bo, &QPushButton::clicked, &dlg, &QDialog::accept );

		QPushButton * bc = new QPushButton( Spell::tr( "Cancel" ), &dlg );
		QObject::connect( bc, &QPushButton::clicked, &dlg, &QDialog::reject );

		QPushButton * bm = nullptr;
		if ( nif->getBSVersion() >= 130 ) {
			bm = new QPushButton( Spell::tr( "Browse Materials" ), &dlg );
			QObject::connect( bm, &QPushButton::clicked, le, [this, le, nif]() { browseMaterial( le, nif ); } );
		}

		QGridLayout * grid = new QGridLayout;
		dlg.setLayout( grid );
		if ( !bm ) {
			grid->addWidget( lb, 0, 0, 1, 2 );
		} else {
			grid->addWidget( lb, 0, 0, 1, 1 );
			grid->addWidget( bm, 0, 1, 1, 1 );
		}
		grid->addWidget( lw, 1, 0, 1, 2 );
		grid->addWidget( le, 2, 0, 1, 2 );
		grid->addWidget( bo, 3, 0, 1, 1 );
		grid->addWidget( bc, 3, 1, 1, 1 );

		if ( dlg.exec() != QDialog::Accepted )
			return index;

		if ( le->text() != string )
			nif->set<QString>( index, le->text() );

		return index;
	}
};

static bool bgsmFileNameFilterFunc( [[maybe_unused]] void * p, const std::string_view & s )
{
	return ( s.starts_with( "materials/" ) && ( s.ends_with( ".bgsm" ) || s.ends_with( ".bgem" ) ) );
}

QString spEditStringIndex::browseMaterial( const NifModel * nif, const QString & matPath )
{
	std::set< std::string_view >	materials;
	AllocBuffers	stringBuf;
	quint32	bsVersion = nif->getBSVersion();
	if ( bsVersion < 170 ) {
		nif->listResourceFiles( materials, &bgsmFileNameFilterFunc );
	} else {
		const CE2MaterialDB * matDB = nif->getCE2Materials();
		if ( matDB )
			matDB->getMaterialList( materials, stringBuf );
	}

	std::string	prvPath;
	if ( !matPath.isEmpty() )
		prvPath = Game::GameManager::get_full_path( matPath, "materials", ( bsVersion >= 170 ? ".mat" : nullptr ) );

	FileBrowserWidget	fileBrowser( 800, 600, "Select Material", materials, prvPath );
	if ( fileBrowser.exec() == QDialog::Accepted ) {
		const std::string_view *	s = fileBrowser.getItemSelected();
		if ( s )
			return QString::fromUtf8( s->data(), qsizetype(s->length()) );
	}
	return QString();
}

void spEditStringIndex::browseMaterial( QLineEdit * le, const NifModel * nif )
{
	QString	newPath( browseMaterial( nif, le->text() ) );
	if ( !newPath.isEmpty() )
		le->setText( newPath );
}

REGISTER_SPELL( spEditStringIndex )

//! Choose a material path
class spBrowseMaterialPath final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Choose" ); }
	QString page() const override final { return Spell::tr( "Material" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool constant() const override final { return true; }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( !( nif && nif->getBSVersion() >= 130 && index.isValid() ) )
			return false;
		if ( nif->getBSVersion() >= 170 )
			return nif->blockInherits( index, "BSGeometry" ) || nif->blockInherits( index, "BSLightingShaderProperty" );
		return ( nif->blockInherits( index, "BSTriShape" ) || nif->blockInherits( index, "BSLightingShaderProperty" )
				|| nif->blockInherits( index, "BSEffectShaderProperty" ) );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		QModelIndex	idx = index;
		if ( nif->blockInherits( idx, ( nif->getBSVersion() < 170 ? "BSTriShape" : "BSGeometry" ) ) )
			idx = nif->getBlockIndex( nif->getLink( idx, "Shader Property" ) );
		if ( nif->blockInherits( idx, "BSShaderProperty" ) )
			idx = nif->getIndex( idx, "Name" );
		else
			return index;
		if ( idx.isValid() ) {
			spEditStringIndex	sp;
			(void) sp.cast( nif, idx );
		}
		return index;
	}
};

REGISTER_SPELL( spBrowseMaterialPath )

//! Browse a material path stored as header string
class spBrowseHeaderMaterialPath final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Browse Material" ); }
	QString page() const override final { return Spell::tr( "" ); }
	QIcon icon() const override final
	{
		if ( !txt_xpm_icon )
			txt_xpm_icon = QIconPtr( new QIcon(QPixmap( txt_xpm )) );

		return *txt_xpm_icon;
	}
	bool constant() const override final { return true; }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( !( nif && nif->getBSVersion() >= 130 ) )
			return false;
		auto block = nif->getTopItem( index );
		if ( !( block && block == nif->getHeaderItem() ) )
			return false;
		const NifItem *	item = nif->getItem( index );
		if ( !( item && item->valueType() == NifValue::tSizedString ) )
			return false;
		QString	s( item->getValueAsString() );
		if ( nif->getBSVersion() < 170 )
			return ( s.endsWith( ".bgsm", Qt::CaseInsensitive ) || s.endsWith( ".bgem", Qt::CaseInsensitive ) );
		return ( s.endsWith( ".mat", Qt::CaseInsensitive ) || s == "MATERIAL_PATH" );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		NifItem *	item = nif->getItem( index );
		if ( !( item && item->valueType() == NifValue::tSizedString ) )
			return index;

		QString	newPath( spEditStringIndex::browseMaterial( nif, item->getValueAsString() ) );
		if ( !newPath.isEmpty() )
			item->setValueFromString( newPath );

		return index;
	}
};

REGISTER_SPELL( spBrowseHeaderMaterialPath )
