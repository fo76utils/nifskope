#include "spellbook.h"

#include <QDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

#include <QFileDialog>
#include <QHash>
#include <QFile>
#include <QTextStream>
#include <QDir>

// Brief description is deliberately not autolinked to class Spell
/*! \file meshreplace.cpp
 * \brief Spell to replace Starfield mesh paths from an oldpath:newpath file (spMeshUpdate)
 *
 * All classes here inherit from the Spell class.
 */

//! Replace SF Mesh Paths
class spMeshUpdate final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Replace Mesh Paths TESTING" ); }
	QString page() const override final { return Spell::tr( "" ); }
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


	QHash<QString, QString> loadMapFile(const QString& filename);
	void replacePaths(NifModel *nif, NifItem *item, const QHash<QString, QString> &pathMap);
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};


QHash<QString, QString> spMeshUpdate::loadMapFile(const QString& filename) {
    QHash<QString, QString> pathMap;

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Handle error opening file
        return pathMap;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList parts = line.split(':');
        if (parts.size() == 2) {
            QString key = parts[0].trimmed();
            QString value = parts[1].trimmed();
            pathMap.insert(key, value);
        }
    }

    return pathMap;
}

void spMeshUpdate::replacePaths(NifModel *nif, NifItem *item, const QHash<QString, QString> &pathMap)
{
	//#TODO: Restrict to only LOD mesh paths
	if ( item && item->value().isString() && ( item->name().endsWith( "Path" ) ) ) {
		QString	itemValue( item->getValueAsString() );
		if ( pathMap.contains(itemValue) ) {
			item->setValueFromString( pathMap.value(itemValue) );
		}
	}

	for ( int i = 0; i < item->childCount(); i++ ) {
		if ( item->child( i ) )
			replacePaths( nif, item->child( i ), pathMap );
	}
}

QModelIndex spMeshUpdate::cast ( NifModel * nif, const QModelIndex & index )
{
	if ( !nif )
		return index;

    QString executableDir = QCoreApplication::applicationDirPath();

	QString filePath = QDir(executableDir).filePath("sf_mesh_map_1_11_33.txt");

    // Open a file picker dialog to the latest hash map
    QString selectedFilePath = QFileDialog::getOpenFileName(nullptr, "Open SF Mesh Map File", executableDir, "Text Files (*.txt);;All Files (*)", nullptr, QFileDialog::DontUseCustomDirectoryIcons);

    // Check if the user selected a file
    if (selectedFilePath.isEmpty()) {
		return index;
    }

	QHash<QString, QString> meshMap = loadMapFile(selectedFilePath);

	QString msg = "Script will replace old SF mesh paths with new. Are you sure? Safety not Guaranteed...";
	if (meshMap.isEmpty()) {
		msg = "No file loaded - nothing will be modified";
	}

	QDialog dlg;

	QLabel * lb = new QLabel( &dlg );
	lb->setAlignment( Qt::AlignCenter );
	lb->setText( Spell::tr( msg.toUtf8().constData() ) );

	QPushButton * bo = new QPushButton( Spell::tr( "Ok" ), &dlg );
	QObject::connect( bo, &QPushButton::clicked, &dlg, &QDialog::accept );

	QPushButton * bc = new QPushButton( Spell::tr( "Cancel" ), &dlg );
	QObject::connect( bc, &QPushButton::clicked, &dlg, &QDialog::reject );

	QGridLayout * grid = new QGridLayout;
	dlg.setLayout( grid );
	grid->addWidget( lb, 0, 0, 1, 2 );
	grid->addWidget( bo, 7, 0, 1, 1 );
	grid->addWidget( bc, 7, 1, 1, 1 );
	if ( dlg.exec() != QDialog::Accepted )
		return index;

	if (meshMap.isEmpty())
		return index;

	for ( int b = 0; b < nif->getBlockCount(); b++ ) {
		NifItem *	item = nif->getBlockItem( quint32(b) );
		if ( item )
			replacePaths( nif, item, meshMap );
	}

	return index;
}

REGISTER_SPELL( spMeshUpdate )

