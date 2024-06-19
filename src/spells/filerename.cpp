#include "spellbook.h"

#include <QDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>

// Brief description is deliberately not autolinked to class Spell
/*! \file filerename.cpp
 * \brief Spell to modify resource paths with regular expression search and replacement (spResourceRename)
 *
 * All classes here inherit from the Spell class.
 */

//! Edit the index of a header string
class spResourceRename final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Search/Replace Resource Paths" ); }
	QString page() const override final { return Spell::tr( "Batch" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool constant() const override final { return false; }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return ( nif && !index.isValid() );
	}

	void renamePaths( NifModel * nif, NifItem * item, const QRegularExpression & searchPattern, const QString & replacementString, const QRegularExpression & filterPattern );
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

void spResourceRename::renamePaths( NifModel * nif, NifItem * item, const QRegularExpression & searchPattern, const QString & replacementString, const QRegularExpression & filterPattern )
{
	if ( item && item->value().isString() && ( item->name().endsWith( "Path" ) || item->name().startsWith( "Texture" ) ) ) {
		QString	itemValue( item->getValueAsString() );
		if ( itemValue.contains( filterPattern ) ) {
			QString	newValue( itemValue );
			if ( newValue.replace( searchPattern, replacementString ) != itemValue ) {
				if ( QMessageBox::question( nullptr, "Confirm rename", QString( "Replace %1 with %2?" ).arg( itemValue ).arg( newValue ) ) == QMessageBox::Yes ) {
					item->setValueFromString( newValue );
				}
			}
		}
	}

	for ( int i = 0; i < item->childCount(); i++ ) {
		if ( item->child( i ) )
			renamePaths( nif, item->child( i ), searchPattern, replacementString, filterPattern );
	}
}

QModelIndex spResourceRename::cast ( NifModel * nif, const QModelIndex & index )
{
	if ( !nif )
		return index;

	QDialog dlg;

	QLabel * lb = new QLabel( &dlg );
	lb->setAlignment( Qt::AlignCenter );
	lb->setText( Spell::tr( "Search and replace mesh and texture paths" ) );

	QLabel * lb1 = new QLabel( &dlg );
	lb1->setText( Spell::tr( "Regular expression to search for:" ) );
	QLineEdit * le1 = new QLineEdit( &dlg );
	le1->setFocus();

	QLabel * lb2 = new QLabel( &dlg );
	lb2->setText( Spell::tr( "Replacement text:" ) );
	QLineEdit * le2 = new QLineEdit( &dlg );
	le2->setFocus();

	QLabel * lb3 = new QLabel( &dlg );
	lb3->setText( Spell::tr( "Path filter regular expression:" ) );
	QLineEdit * le3 = new QLineEdit( &dlg );
	le3->setFocus();

	QPushButton * bo = new QPushButton( Spell::tr( "Ok" ), &dlg );
	QObject::connect( bo, &QPushButton::clicked, &dlg, &QDialog::accept );

	QPushButton * bc = new QPushButton( Spell::tr( "Cancel" ), &dlg );
	QObject::connect( bc, &QPushButton::clicked, &dlg, &QDialog::reject );

	QGridLayout * grid = new QGridLayout;
	dlg.setLayout( grid );
	grid->addWidget( lb, 0, 0, 1, 2 );
	grid->addWidget( lb1, 1, 0, 1, 2 );
	grid->addWidget( le1, 2, 0, 1, 2 );
	grid->addWidget( lb2, 3, 0, 1, 2 );
	grid->addWidget( le2, 4, 0, 1, 2 );
	grid->addWidget( lb3, 5, 0, 1, 2 );
	grid->addWidget( le3, 6, 0, 1, 2 );
	grid->addWidget( bo, 7, 0, 1, 1 );
	grid->addWidget( bc, 7, 1, 1, 1 );
	if ( dlg.exec() != QDialog::Accepted )
		return index;

	QRegularExpression	searchPattern( le1->text().trimmed(), QRegularExpression::CaseInsensitiveOption );
	QString	replacementString = le2->text().trimmed();
	QRegularExpression	filterPattern( le3->text().trimmed(), QRegularExpression::CaseInsensitiveOption );

	for ( int b = 0; b < nif->getBlockCount(); b++ ) {
		NifItem *	item = nif->getBlockItem( quint32(b) );
		if ( item )
			renamePaths( nif, item, searchPattern, replacementString, filterPattern );
	}

	return index;
}

REGISTER_SPELL( spResourceRename )

