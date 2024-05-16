#include "NifSkope.h"
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
//TODO: Cleanup includes (sort, remove unused)


// Brief description is deliberately not autolinked to class Spell
/*! \file meshreplacebulk.cpp
 * \brief Spell to replace Starfield mesh paths from an oldpath:newpath file (spMeshUpdate)
 *
 * All classes here inherit from the Spell class.
 */

//! Bulk Replace SF Mesh Paths
class spBulkMeshUpdate final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Bulk Update to SF 1.11.33 Mesh Paths" ); }
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
    QList<spBulkMeshUpdate::ReplacementLog> processNif(NifModel * nif, const QHash<QString, QString> &pathMap);

	void replacePaths(NifModel *nif, NifItem *item, const QHash<QString, QString> &pathMap, QList<ReplacementLog> &replacementLogs);
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

QHash<QString, QString> spBulkMeshUpdate::loadMapFile(const QString &filename)
{
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
void spBulkMeshUpdate::replacePaths(NifModel *nif, NifItem *item, const QHash<QString, QString> &pathMap, QList<ReplacementLog> &replacementLogs)
{
	if ( item && item->value().isString() && ( item->name() == "Mesh Path" ) ) {
		QString	itemValue( item->getValueAsString() );

        if (!itemValue.isEmpty()) {
            if (pathMap.contains(itemValue) ) {
                if (!pathMap.value(itemValue).isEmpty()) {
                    item->setValueFromString( pathMap.value(itemValue) );
                }
                ReplacementLog log;
                log.objectName = nif->get<QString>( item->parent()->parent()->parent()->parent(), "Name" );
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


QList<spBulkMeshUpdate::ReplacementLog> spBulkMeshUpdate::processNif(NifModel * nif, const QHash<QString, QString> &pathMap)
{
    QList<ReplacementLog> replacementLogs;

    for ( int b = 0; b < nif->getBlockCount(); b++ ) {
		NifItem *	item = nif->getBlockItem( quint32(b) );
		if ( item )
            replacePaths( nif, item, pathMap , replacementLogs);
	}

    return replacementLogs;
}


QModelIndex spBulkMeshUpdate::cast ( NifModel * nif, const QModelIndex & index )
{
    if ( !nif )
        return index;

    NifSkope* nifSkope = qobject_cast<NifSkope*>(nif->getWindow());

    QString executableDir = QCoreApplication::applicationDirPath();

    QString filePath = QDir(executableDir).filePath("sf_mesh_map_1_11_33.txt");

    QHash<QString, QString> meshMap = loadMapFile(filePath);

    if (meshMap.isEmpty()) {
        QMessageBox::critical(nullptr, "Error", "Problem loading map file\nPlease ensure the file sf_mesh_map_1_11_33.txt is in the same folder as NifSkope.");
        return index;
    }

    QString rootFolder = QFileDialog::getExistingDirectory(nullptr, "Select root folder to process");

    if (rootFolder.isEmpty()) {
        QMessageBox::information(nullptr, "Information", "No folder selected. Operation canceled.");
        return index;
    }

    QDir rootDir(rootFolder);
    if (!rootDir.exists()) {
        QMessageBox::critical(nullptr, "Error", "Selected folder does not exist.");
        return index;
    }

    QString logFilePath = rootDir.filePath("sf_mesh_map_1_11_33_log.txt");
    QFile logFile(logFilePath);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(nullptr, "Error", "Failed to create log file.");
        return index;
    }

    QTextStream logStream(&logFile);
    logStream << "Spell Name: " << name() << "\n";
    logStream << "Date and Time: " << QDateTime::currentDateTime().toString(Qt::DefaultLocaleShortDate) << "\n";

    int filesProcessed = 0;
    int updatesPerformed = 0;
    int unmappedItemsEncountered = 0;

    QDirIterator it(rootFolder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString filePath = it.next();
        if (filePath.endsWith(".nif", Qt::CaseInsensitive)) {
            //TODO: check if file is readonly and if so, skip processing and add message alerting that file coun't be processed

            nifSkope->openFile(filePath);

            QCoreApplication::processEvents();
            QList<ReplacementLog> logs = processNif(nif, meshMap);
            QCoreApplication::processEvents();
            //TODO: Check if there were any changes made to this .nif file, and skip save if none
            nifSkope->publicSave();
            QCoreApplication::processEvents();

            // Write to log file
            QString relativePath = rootDir.relativeFilePath(filePath);
            logStream << "File: " << relativePath << "\n";
            for (const auto &log : logs) {
                logStream << "\"" << log.objectName << "\" " << log.oldPath << " -> " << log.newPath << "\n";
                if (log.newPath == "ERROR_NOT_MAPPED")
                    unmappedItemsEncountered++;
                else
                    updatesPerformed++;
            }
            filesProcessed++;
        }
    }
    //TODO: Update summary to include count of files that could not be processed

    // Show summary
    QString summaryMsg = QString("Files processed: %1\nUpdates performed: %2\nUnmapped items encountered: %3").arg(filesProcessed).arg(updatesPerformed).arg(unmappedItemsEncountered);
    QMessageBox::information(nullptr, "Summary", summaryMsg);

    logFile.close();
    return index;
}

REGISTER_SPELL(spBulkMeshUpdate)
