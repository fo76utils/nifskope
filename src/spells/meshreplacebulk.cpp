#include "../NifSkope.h"
#include "spellbook.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QHash>
#include <QMessageBox>
#include <QTextStream>
#include <string>
//TODO: Handle load and save comletion events properly
//TODO: Move mesh map path into starfield settings



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
    //Spell Implementation
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

    QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
    //End Spell Implementation

    //Represents a replacement made (or not) by script
    struct ReplacementLog {
        QString objectName;
        QString oldPath;
        QString newPath;
    };

    //Load colon delimited map from old path to new path
    QHash<QString, QString> loadMapFile(const QString &filename);
    //Process a loaded nif structure
    QList<spBulkMeshUpdate::ReplacementLog> processNif(NifModel * nif, const QHash<QString, QString> &pathMap);
    //Recurse the nif structure looking for all Mesh Paths
	void replacePaths(NifModel *nif, NifItem *item, const QHash<QString, QString> &pathMap, QList<ReplacementLog> &replacementLogs);

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
        QStringList parts = line.split(':', Qt::KeepEmptyParts); // Use KeepEmptyParts to keep empty strings after split
        if (parts.size() >= 1) {
            QString key = parts[0].trimmed();
            QString value = (parts.size() > 1) ? parts[1].trimmed() : ""; // Handle cases where there is no value after the colon
            pathMap.insert(key, value);
        }
    }

    return pathMap;
}



void spBulkMeshUpdate::replacePaths(NifModel *nif, NifItem *item, const QHash<QString, QString> &pathMap, QList<ReplacementLog> &replacementLogs)
{
    //Only CE2+ .nifs have a "Mesh Path"
	if ( item && item->value().isString() && ( item->name() == "Mesh Path" ) ) {
		QString	itemValue( item->getValueAsString() );

        if (!itemValue.isEmpty()) {
            if (pathMap.contains(itemValue) ) {
                //Note: pathMap v2+ contains complete set of source paths with blanks for unmapped
                if (!pathMap.value(itemValue).isEmpty()) {
                    //Do not update with a blank - blanks are unmapped
                    item->setValueFromString( pathMap.value(itemValue) );
                }
                ReplacementLog log;
                //Get the name of the containing BSGeometry object
                //Note: these are supposed to be unique and populated, but aren't always
                //Pre-sanitizing would resolve such issues
                log.objectName = nif->get<QString>( item->parent()->parent()->parent()->parent(), "Name" );
                log.oldPath = itemValue;
                log.newPath = pathMap.value(itemValue).isEmpty() ? "ERROR_NOT_MAPPED" : pathMap.value(itemValue);
                replacementLogs.append(log);
            }
        }
    }

    //Process children
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
    QDateTime startTime(QDateTime::currentDateTime());

    //Reference to NifSkope required for successful saving
    NifSkope* nifSkope = qobject_cast<NifSkope*>(nif->getWindow());

    QString executableDir = QCoreApplication::applicationDirPath();

    QString filePath = QDir(executableDir).filePath("sf_mesh_map_1_11_33.v2.txt");

    QHash<QString, QString> meshMap = loadMapFile(filePath);

    if (meshMap.isEmpty()) {
        QMessageBox::critical(nullptr, "Error", "Problem loading map file\nPlease ensure the file sf_mesh_map_1_11_33.v2.txt is in the same folder as NifSkope.");
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


    QString logFileName = QString("sf_mesh_map_1_11_33_log_%1.txt").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss"));


    QString logFilePath = rootDir.filePath(logFileName);
    QFile logFile(logFilePath);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(nullptr, "Error", "Failed to create log file.");
        return index;
    }

    QTextStream logStream(&logFile);
    logStream << "Spell Name: " << name() << "\n";
    logStream << "Date and Time: " << startTime.toString("yyyy-MM-dd hh:mm:ss") << "\n";

    int filesProcessed = 0;
    int updatesPerformed = 0;
    int unmappedItemsEncountered = 0;
    int readOnlyCount = 0;

    QDirIterator it(rootFolder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString filePath = it.next();
        if (filePath.endsWith(".nif", Qt::CaseInsensitive)) {
            QFile file(filePath);
            //Nifskope doesn't report an error when trying to save to a readonly file fails
            if (!(file.permissions() & QFile::WriteUser)) {
                qWarning() << "Skipping read-only file:" << filePath;
                readOnlyCount++;
                continue;
            }

            nifSkope->openFile(filePath);
            //Temporary solution
            //Correct solution must await a signal and check if load succeeded
            //This probaly will need to be moved into NifSkope.h/cpp
            //Without the processEvents, changes are not applied to saved files.
            QCoreApplication::processEvents();
            QList<ReplacementLog> logs = processNif(nif, meshMap);
            
            if (!logs.isEmpty()) {
                QCoreApplication::processEvents(); //Belt... (not clickbait)

                //NifSkope class had no public save method - temporary until moving logic into NifSkope class (for events)
                nifSkope->publicSave();
                QCoreApplication::processEvents(); //... And suspenders :P
            } else {
                qWarning() << "No changes made to the file: " << filePath;
            }
            //Use path relative to the root folder in the report
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
    // Show summary
    QString summaryMsg = QString("Files processed: %1\nRead-only files skipped: %2\nUpdates performed: %3\nUnmapped items encountered: %4")
        .arg(filesProcessed).arg(readOnlyCount).arg(updatesPerformed).arg(unmappedItemsEncountered);
    QMessageBox::information(nullptr, "Summary", summaryMsg);
    logFile.close();

    return index;
}

REGISTER_SPELL(spBulkMeshUpdate)
