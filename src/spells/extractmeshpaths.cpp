#include "spellbook.h"

#include <QFileDialog>
#include <QHash>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDateTime>
#include <QDirIterator>

// Brief description is deliberately not autolinked to class Spell
/*! \file extractmeshpaths.cpp
 * \brief Spell to replace Starfield mesh paths from an oldpath:newpath file (spExtractMeshPaths)
 *
 * All classes here inherit from the Spell class.
 */

//! Extract Mesh Path Information
class spExtractMeshPaths final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Extract Mesh Paths" ); }
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

    void processGeometries(NifModel *nif, NifItem *item, QTextStream *output, const QString &filePath);
    NifItem* findChildByName(NifItem* parent, const QString& name);
    QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};


NifItem* spExtractMeshPaths::findChildByName(NifItem* parent, const QString& name)
{
    for (int i = 0; i < parent->childCount(); i++) {
        NifItem* child = parent->child(i);
        if (child && child->name() == name) {
            return child;
        }
    }
    return nullptr;
}


void spExtractMeshPaths::processGeometries(NifModel *nif, NifItem *item, QTextStream *output, const QString &filePath)
{
    //Nif geometry object structure sort of
/*    <niobject name="BSGeometry" inherit="NiAVObject" module="BSMain" versions="#STF#">
        <field name="Bounding Sphere" type="NiBound" />
        <field name="Bound Min Max" type="float" length="6" />
        <field name="Skin" type="Ref" template="NiObject" />
        <field name="Shader Property" type="Ref" template="BSShaderProperty" />
        <field name="Alpha Property" type="Ref" template="NiAlphaProperty" />
        <field name="Meshes" type="BSMeshArray" length="4" />
		        <field name="Has Mesh" type="byte" />
		        <field name="Mesh" type="BSMesh" cond="Has Mesh #EQ# 1" />
			        <field name="Indices Size" type="uint" />
			        <field name="Num Verts" type="uint" />
			        <field name="Flags" type="uint" />
			        <field name="Mesh Path" type="SizedString" />*/

    if (item && item->name() == "BSGeometry") {
        QString objectName = nif->get<QString>(item, "Name");
        NifItem* meshArrayItems = findChildByName(item, "Meshes");

        if (meshArrayItems) {
            for (int i = 0; i < meshArrayItems->childCount(); i++) {
                NifItem *meshArrayItem = meshArrayItems->child(i);
                if (!nif->get<bool>(meshArrayItem, "Has Mesh")) {
                    continue;
                }
                NifItem *mesh = findChildByName(meshArrayItem, "Mesh");
                if (mesh) {
                    QString meshPath = nif->get<QString>(mesh, "Mesh Path");
                    //todo: escape " characters in objectName (check if needed)
                    *output << "\"" << filePath << "\",\"" << objectName << "\",\"" << QString::number(i + 1) << "\",\"" << meshPath << "\"\r\n";
                    output->flush(); // Flush after each write
                }
            }
        }
        // BSGeometri are leaf structures so no need to process children
    } else {
        // Process children
        for (int i = 0; i < item->childCount(); i++) {
            if (item->child(i)) {
                processGeometries(nif, item->child(i), output, filePath);
            }
        }
    }
}

QModelIndex spExtractMeshPaths::cast(NifModel *nif, const QModelIndex &index)
{
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

    QString logFileName = QString("mesh_paths_%1.csv").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss"));
    QString logFilePath = rootDir.filePath(logFileName);
    QFile logFile(logFilePath);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(nullptr, "Error", "Failed to create log file.");
        return index;
    }

    QTextStream logStream(&logFile);
    logStream << "File Path, Object Name, LOD Number, Mesh Path\n";
    logStream.flush(); // Initial flush to ensure the header is written

    QDirIterator it(rootFolder, QDir::Files, QDirIterator::Subdirectories);
    NifModel tempNifModel(nif->getWindow());
    QString errorLogFilePath = rootDir.filePath("unreadable_files.log");

    while (it.hasNext()) {
        QString filePath = it.next();
        if (filePath.endsWith(".nif", Qt::CaseInsensitive)) {
            tempNifModel.clear();

            if (tempNifModel.loadFromFile(filePath)) {
                for (int b = 0; b < tempNifModel.getBlockCount(); b++) {
                    NifItem *item = tempNifModel.getBlockItem(quint32(b));
                    if (item) {
                        processGeometries(&tempNifModel, item, &logStream, rootDir.relativeFilePath(filePath));
                    }
                }
            } else {
                // Log failed nifs to a separate file for reporting to NifSkope devs
                QFile errorLogFile(errorLogFilePath);
                if (errorLogFile.open(QIODevice::Append | QIODevice::Text)) {
                    QTextStream errorLogStream(&errorLogFile);
                    errorLogStream << "Failed to read: " << filePath << "\n";
                    errorLogFile.close();
                }
            }
        }
    }

    logFile.close();
    QMessageBox::information(nullptr, "Information", "Data extraction complete.");

    return index;
}

//Disabling to avoid confusion
//REGISTER_SPELL(spExtractMeshPaths)
