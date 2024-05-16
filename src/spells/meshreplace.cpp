#include "spellbook.h"

#include <QDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <string>

//#include <QFileDialog>
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
	QString name() const override final { return Spell::tr( "Update to SF 1.11.33 Mesh Paths" ); }
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

    struct updateStats {
        int matchedCnt = 0;
		int replaceCnt = 0;
    };

	QHash<QString, QString> loadMapFile(const QString& filename);
	void processNif(NifModel * nif, const QHash<QString, QString> &pathMap);

	void replacePaths(NifModel *nif, NifItem *item, const QHash<QString, QString> &pathMap, const QRegularExpression &regex, updateStats &stats);
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

//lookup all mesh paths in qhash and replace if found.
void spMeshUpdate::replacePaths(NifModel *nif, NifItem *item, const QHash<QString, QString> &pathMap, const QRegularExpression &regex, updateStats &stats)
{
	if ( item && item->value().isString() && ( item->name() == "Mesh Path" ) ) {
		QString	itemValue( item->getValueAsString() );

		if (!itemValue.isEmpty()) {
			if (regex.isValid() && regex.match(itemValue).hasMatch()) {
				stats.matchedCnt++;
			}
			if (pathMap.contains(itemValue) ) {
				item->setValueFromString( pathMap.value(itemValue) );
				stats.replaceCnt++;
			}
		}
	}

	for ( int i = 0; i < item->childCount(); i++ ) {
		if ( item->child( i ) ){
			replacePaths( nif, item->child( i ), pathMap , regex, stats);
		}
	}
}

void spMeshUpdate::processNif(NifModel * nif, const QHash<QString, QString> &pathMap)
{
	updateStats stats;
	QRegularExpression regex("^[0-9a-f]{20}\\\\[0-9a-f]{20}$");

	for ( int b = 0; b < nif->getBlockCount(); b++ ) {
		NifItem *	item = nif->getBlockItem( quint32(b) );
		if ( item )
			replacePaths( nif, item, pathMap , regex , stats);
	}
	std::string msg = "Updated " + std::to_string(stats.replaceCnt) + " out of " + std::to_string(stats.matchedCnt) + " vanilla looking meshes";
	QDialog dlg;
	dlg.setWindowTitle("Mesh Update Results");
	QLabel * lb = new QLabel( &dlg );
	lb->setAlignment( Qt::AlignCenter );
	lb->setText( Spell::tr( QString::fromStdString(msg).toUtf8().constData() ) );

	QPushButton * bo = new QPushButton( Spell::tr( "Ok" ), &dlg );
	QObject::connect(bo, &QPushButton::clicked, &dlg, &QDialog::accept);

	QGridLayout * grid = new QGridLayout;
	dlg.setLayout( grid );
	grid->addWidget( lb, 0, 0, 1, 2 );
	grid->addWidget( bo, 7, 0, 1, 1 );
	dlg.exec();

}

QModelIndex spMeshUpdate::cast ( NifModel * nif, const QModelIndex & index )
{
	if ( !nif )
		return index;

    QString executableDir = QCoreApplication::applicationDirPath();

	QString filePath = QDir(executableDir).filePath("sf_mesh_map_1_11_33.v2.txt");

	QHash<QString, QString> meshMap = loadMapFile(filePath);

	if (meshMap.isEmpty()) 
	{
		//TODO: translations
		QMessageBox::critical(nullptr, "Error", "Problem loading map file\nPlease ensure the file sf_mesh_map_1_11_33.v2.txt is in the same folder as NifSkope.");
		return index;
	}

	processNif(nif, meshMap);
	return index;
}

REGISTER_SPELL( spMeshUpdate )

