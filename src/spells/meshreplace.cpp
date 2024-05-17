#include "spellbook.h"

#include <QDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <string>

#include <QFileDialog>
#include <QHash>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDateTime>
#include <QDirIterator>

// Brief description is deliberately not autolinked to class Spell
/*! \file meshreplace.cpp
 * \brief Spell to replace Starfield mesh paths from an oldpath:newpath file (spMeshUpdate)
 *
 * All classes here inherit from the Spell class.
 */

//! Replace SF Mesh Paths in the currently open file
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

    struct ReplacementLog {
        QString objectName;
        QString oldPath;
        QString newPath;
    };

    QHash<QString, QString> loadMapFile(const QString &filename);
    QList<spMeshUpdate::ReplacementLog> processNif(NifModel *nif, const QHash<QString, QString> &pathMap);

    void replacePaths(NifModel *nif, NifItem *item, const QHash<QString, QString> &pathMap, QList<ReplacementLog> &replacementLogs);
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

QHash<QString, QString> spMeshUpdate::loadMapFile(const QString &filename)
{
    QHash<QString, QString> pathMap;

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return pathMap;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList parts = line.split(':', Qt::KeepEmptyParts);
        if (parts.size() >= 1) {
            QString key = parts[0].trimmed();
            QString value = (parts.size() > 1) ? parts[1].trimmed() : "";
            pathMap.insert(key, value);
        }
    }

    return pathMap;
}

void spMeshUpdate::replacePaths(NifModel *nif, NifItem *item, const QHash<QString, QString> &pathMap, QList<ReplacementLog> &replacementLogs)
{
	if ( item && item->value().isString() && ( item->name() == "Mesh Path" ) ) {
		QString	itemValue( item->getValueAsString() );

        if (!itemValue.isEmpty()) {
			// if (regex.isValid() && regex.match(itemValue).hasMatch()) {
			// 	stats.matchedCnt++;
			// }
            if (pathMap.contains(itemValue) ) {
                if (!pathMap.value(itemValue).isEmpty()) {
                    item->setValueFromString( pathMap.value(itemValue) );
                }
                ReplacementLog log;
                log.objectName = nif->get<QString>(item->parent()->parent()->parent()->parent(), "Name");
                log.oldPath = itemValue;
                log.newPath = pathMap.value(itemValue).isEmpty() ? "ERROR_NOT_MAPPED" : pathMap.value(itemValue);
                replacementLogs.append(log);
            }
        }
    }

	for ( int i = 0; i < item->childCount(); i++ ) {
		if ( item->child( i ) ){
			replacePaths( nif, item->child( i ), pathMap , replacementLogs);
        }
    }
}

QList<spMeshUpdate::ReplacementLog> spMeshUpdate::processNif(NifModel *nif, const QHash<QString, QString> &pathMap)
{
    QList<ReplacementLog> replacementLogs;


	for ( int b = 0; b < nif->getBlockCount(); b++ ) {
		NifItem *	item = nif->getBlockItem( quint32(b) );
		if ( item )
			replacePaths( nif, item, pathMap , replacementLogs);
	}

    return replacementLogs;
}

QModelIndex spMeshUpdate::cast ( NifModel * nif, const QModelIndex & index )
{
	if ( !nif )
		return index;

    QString executableDir = QCoreApplication::applicationDirPath();

	QString filePath = QDir(executableDir).filePath("sf_mesh_map_1_11_33.v2.txt");

    QHash<QString, QString> meshMap = loadMapFile(filePath);

	if (meshMap.isEmpty()) {
        QMessageBox::critical(nullptr, "Error", "Problem loading map file\nPlease ensure the file sf_mesh_map_1_11_33.v2.txt is in the same folder as NifSkope.");
        return index;
    }

    QString logFileName = QString("sf_mesh_map_1_11_33.v2._log_%1.txt").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss"));
    QString logFilePath = QDir(executableDir).filePath(logFileName);
    QFile logFile(logFilePath);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(nullptr, "Error", "Failed to create log file.");
        return index;
    }

    QTextStream logStream(&logFile);
    logStream << "Spell Name: " << name() << "\n";
    logStream << "Date and Time: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";

    int updatesPerformed = 0;
    int unmappedItemsEncountered = 0;

    QList<ReplacementLog> logs = processNif(nif, meshMap);

    // if (logs.isEmpty()) {
    //     qWarning() << "No changes made to the file.";
    // }

    for (const auto &log : logs) {
        logStream << "\"" << log.objectName << "\" " << log.oldPath << " -> " << log.newPath << "\n";
        if (log.newPath == "ERROR_NOT_MAPPED")
            unmappedItemsEncountered++;
        else
            updatesPerformed++;
    }

    QString summaryMsg = QString("Updates performed: %1\nUnmapped items encountered: %2")
        .arg(updatesPerformed).arg(unmappedItemsEncountered);
    QMessageBox::information(nullptr, "Summary", summaryMsg);
    logFile.close();

    return index;
}

REGISTER_SPELL(spMeshUpdate)
