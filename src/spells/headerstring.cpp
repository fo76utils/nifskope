#include "spellbook.h"

#include <QDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTreeWidget>

#include "gamemanager.h"
#include "libfo76utils/src/common.hpp"
#include "libfo76utils/src/material.hpp"

class FileBrowserWidget
{
protected:
	QDialog	dlg;
	QGridLayout *	layout;
	QLabel *	title;
	QTreeWidget *	treeWidget;
	QGridLayout *	layout2;
	QLineEdit *	filter;
	QLabel *	filterTitle;
	const std::set< std::string > &	fileSet;
	const std::string *	currentFile;
	std::vector< const std::string * >	filesShown;
	QTreeWidgetItem *	findDirectory( std::map< std::string, QTreeWidgetItem * > & dirMap, const std::string& d );
	void updateTreeWidget();
	void checkItemActivated();
public:
	FileBrowserWidget( int w, int h, const char * titleString, const std::set< std::string > & files, const std::string& fileSelected );
	~FileBrowserWidget();
	int exec()
	{
		return dlg.exec();
	}
	const std::string *	getItemSelected() const;
};

QTreeWidgetItem *	FileBrowserWidget::findDirectory( std::map< std::string, QTreeWidgetItem * > & dirMap, const std::string& d )
{
	std::map< std::string, QTreeWidgetItem * >::iterator	i = dirMap.find( d );
	if ( i != dirMap.end() )
		return i->second;
	size_t	n = std::string::npos;
	if ( d.length() >= 2 )
		n = d.rfind( '/', d.length() - 2 );
	if ( n == std::string::npos )
		n = 0;
	else
		n++;
	QTreeWidgetItem *	parent = nullptr;
	if ( n )
		parent = findDirectory( dirMap, std::string( d, 0, n ) );
	QTreeWidgetItem *	tmp;
	if ( !parent ) {
		tmp = new QTreeWidgetItem( treeWidget, -2 );
		treeWidget->addTopLevelItem( tmp );
	} else {
		tmp = new QTreeWidgetItem( parent, -2 );
		parent->addChild( tmp );
	}
	dirMap.emplace( d, tmp );
	tmp->setText( 0, QString::fromStdString( std::string( d, n, d.length() - n ) ) );
	return tmp;
}

void FileBrowserWidget::updateTreeWidget()
{
	treeWidget->clear();
	filesShown.clear();
	std::string	filterString( filter->text().trimmed().toStdString() );
	int	curFileIndex = -1;
	for ( std::set< std::string >::const_iterator i = fileSet.begin(); i != fileSet.end(); i++ ) {
		if ( currentFile && *i == *currentFile ) {
			curFileIndex = int( filesShown.size() );
		} else if ( !filterString.empty() && i->find( filterString ) == std::string::npos ) {
			continue;
		}
		filesShown.push_back( &(*i) );
	}

	std::map< std::string, QTreeWidgetItem * >	dirMap;
	std::string	d;
	for ( size_t i = 0; i < filesShown.size(); i++ ) {
		const std::string &	fullPath( *(filesShown[i]) );
		size_t	n = std::string::npos;
		if ( filesShown.size() > 100 )
			n = fullPath.rfind( '/' );
		if ( n == std::string::npos )
			n = 0;
		else
			n++;
		QTreeWidgetItem *	parent = nullptr;
		if ( n ) {
			d = std::string( fullPath, 0, n );
			parent = findDirectory( dirMap, d );
		}
		QTreeWidgetItem *	tmp;
		if ( !parent ) {
			tmp = new QTreeWidgetItem( treeWidget, int(i) );
			treeWidget->addTopLevelItem( tmp );
		} else {
			tmp = new QTreeWidgetItem( parent, int(i) );
			parent->addChild( tmp );
		}
		tmp->setText( 0, QString::fromStdString( std::string( *(filesShown[i]), n, filesShown[i]->length() - n ) ) );
		if ( i == size_t(curFileIndex) )
			treeWidget->setCurrentItem( tmp );
	}
}

void FileBrowserWidget::checkItemActivated()
{
	if ( getItemSelected() )
		dlg.accept();
}

FileBrowserWidget::FileBrowserWidget( int w, int h, const char * titleString, const std::set< std::string > & files, const std::string& fileSelected )
	: fileSet( files ), currentFile( nullptr )
{
	layout = new QGridLayout( &dlg );
	layout->setColumnMinimumWidth( 0, w );
	layout->setRowMinimumHeight( 1, h );
	title = new QLabel( &dlg );
	title->setText( titleString );
	layout->addWidget( title, 0, 0 );
	treeWidget = new QTreeWidget( &dlg );
	treeWidget->setHeaderLabel( "Path" );
	layout->addWidget( treeWidget, 1, 0 );
	layout2 = new QGridLayout( &dlg);
	layout2->setColumnMinimumWidth( 0, w - ( w >> 2 ) );
	layout2->setColumnMinimumWidth( 1, w >> 2 );
	filter = new QLineEdit( &dlg );
	layout2->addWidget( filter, 0, 0 );
	filterTitle = new QLabel( &dlg );
	filterTitle->setText( "Path Filter" );
	layout2->addWidget( filterTitle, 0, 1 );
	layout->addLayout( layout2, 2, 0 );

	if ( !fileSelected.empty() )
		currentFile = &fileSelected;
	QObject::connect( filter, &QLineEdit::returnPressed, filter, [this]() { updateTreeWidget(); } );
	QObject::connect( treeWidget, &QTreeWidget::itemDoubleClicked, treeWidget, [this]() { checkItemActivated(); } );
	updateTreeWidget();
}

FileBrowserWidget::~FileBrowserWidget()
{
}

const std::string * FileBrowserWidget::getItemSelected() const
{
	QList< QTreeWidgetItem * >	tmp = treeWidget->selectedItems();
	if ( tmp.size() > 0 ) {
		int	n = tmp[0]->type();
		if ( n >= 0 && size_t(n) < filesShown.size() )
			return filesShown[n];
	}
	return nullptr;
}

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

	void browseStarfieldMaterial( QLineEdit * le );

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
		if ( nif->getBSVersion() >= 172 ) {
			bm = new QPushButton( Spell::tr( "Browse Materials" ), &dlg );
			QObject::connect( bm, &QPushButton::clicked, le, [this, le]() { browseStarfieldMaterial( le ); } );
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

void spEditStringIndex::browseStarfieldMaterial( QLineEdit * le )
{
	std::set< std::string >	materials;
	const CE2MaterialDB * matDB = Game::GameManager::materials( Game::STARFIELD );
	if ( matDB )
			matDB->getMaterialList( materials );

	std::string	prvPath;
	if ( !le->text().isEmpty() )
		prvPath = Game::GameManager::get_full_path( le->text(), "materials", ".mat" );

	FileBrowserWidget	fileBrowser( 800, 600, "Select Material", materials, prvPath );
	if ( fileBrowser.exec() == QDialog::Accepted ) {
		const std::string *	s = fileBrowser.getItemSelected();
		if ( s )
			le->setText( QString::fromStdString( *s ) );
	}
}

REGISTER_SPELL( spEditStringIndex )

