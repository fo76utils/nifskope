#include "nifskope.h"
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

// Brief description is deliberately not autolinked to class Spell
/*! \file meshclonebulk.cpp
 * \brief Spell to clone Starfield mesh to a new path (spBulkMeshClone)
 *
 * All classes here inherit from the Spell class.
 */

//! Bulk Clone SF Mesh Paths
class spBulkMeshClone final : public Spell
{
public:
    //Spell Implementation
	QString name() const override final { return Spell::tr( "Bulk Clone vanilla meshes" ); }
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

    //Process a loaded nif structure
    QList<spBulkMeshClone::ReplacementLog> processNif(NifModel * nif);
    //Recurse the nif structure looking for all Mesh Paths
	void replacePaths(NifModel *nif, NifItem *item, QList<ReplacementLog> &replacementLogs);

    // Sanitize object name for file paths
    QString sanitizeObjectName(const QString &objectName);

    // Copy the mesh file to the new structure
    bool copyMeshFile(const QString &oldPath, const QString &newPath);
};

QString spBulkMeshClone::sanitizeObjectName(const QString &objectName)
{
    QString sanitized = objectName;
    // Implement your sanitization logic here
    // For example, replace spaces with underscores and remove invalid characters
    sanitized.replace(' ', '_');
    // Add more rules as needed
    return sanitized;
}

bool spBulkMeshClone::copyMeshFile(const QString &oldPath, const QString &newPath)
{
    QFile file(oldPath);
    if (!file.exists()) {
        qWarning() << "File does not exist:" << oldPath;
        return false;
    }

    QDir newDir = QFileInfo(newPath).dir();
    if (!newDir.exists()) {
        newDir.mkpath(".");
    }

    return file.copy(newPath);
}

void spBulkMeshClone::replacePaths(NifModel *nif, NifItem *item, QList<ReplacementLog> &replacementLogs)
{
    // Only CE2+ .nifs have a "Mesh Path"
	if ( item && item->value().isString() && ( item->name() == "Mesh Path" ) ) {
		QString itemValue( item->getValueAsString() );

        if (!itemValue.isEmpty()) {
            // Extract object name and LOD from the existing path
            QStringList parts = itemValue.split('/');
            if (parts.size() == 2) {
                QString folderName = parts[0];
                QString fileName = parts[1];
                QString objectName = sanitizeObjectName(folderName);  // Assuming object name is the folder name
                QString newFilePath = QString("../geometries/onek/%1_lod1.mesh").arg(objectName);
                QString newMeshPath = QString("onek/%1_lod1").arg(objectName);

                // Copy the file to the new structure
                QString oldMeshFilePath = QString("../geometries/%1/%2.mesh").arg(folderName).arg(fileName);
                if (copyMeshFile(oldMeshFilePath, newFilePath)) {
                    item->setValueFromString(newMeshPath);

                    ReplacementLog log;
                    log.objectName = objectName;
                    log.oldPath = itemValue;
                    log.newPath = newMeshPath;
                    replacementLogs.append(log);
                }
            }
        }
    }

    // Process children
    for ( int i = 0; i < item->childCount(); i++ ) {
        if ( item->child( i ) ){
            replacePaths( nif, item->child( i ), replacementLogs);
        }
    }
}

QList<spBulkMeshClone::ReplacementLog> spBulkMeshClone::processNif(NifModel * nif)
{
    QList<ReplacementLog> replacementLogs;

    for ( int b = 0; b < nif->getBlockCount(); b++ ) {
		NifItem *	item = nif->getBlockItem( quint32(b) );
		if ( item )
            replacePaths( nif, item, replacementLogs);
	}

    return replacementLogs;
}

QModelIndex spBulkMeshClone::cast ( NifModel * nif, const QModelIndex & index )
{
    if ( !nif )
        return index;
    QDateTime startTime(QDateTime::currentDateTime());

    //Reference to NifSkope required for successful saving
    NifSkope* nifSkope = qobject_cast<NifSkope*>(nif->getWindow());

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

    QString logFileName = QString("sf_mesh_update_log_%1.txt").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss"));

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
            //This probably will need to be moved into NifSkope.h/cpp
            //Without the processEvents, changes are not applied to saved files.
            QCoreApplication::processEvents();
            QList<ReplacementLog> logs = processNif(nif);

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
                updatesPerformed++;
            }
            filesProcessed++;
        }
    }
    // Show summary
    QString summaryMsg = QString("Files processed: %1\nRead-only files skipped: %2\nUpdates performed: %3")
        .arg(filesProcessed).arg(readOnlyCount).arg(updatesPerformed);
    QMessageBox::information(nullptr, "Summary", summaryMsg);
    logFile.close();

    return index;
}

//REGISTER_SPELL(spBulkMeshClone)
