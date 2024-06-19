#include "spellbook.h"

#include <QDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextBoundaryFinder>

// Brief description is deliberately not autolinked to class Spell
/*! \file meshfilecopy.cpp
 * \brief Spell to modify resource paths into user named folder (spResourceCopy)
 *
 * All classes here inherit from the Spell class.
 */

//! Edit the index of a header string
class spResourceCopy final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Copy and Rename all Meshes" ); }
	QString page() const override final { return Spell::tr( "Batch" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool constant() const override final { return false; }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return ( nif && nif->getBSVersion() >= 170 && !index.isValid() );
	}

	void copyPaths(NifModel* nif, NifItem* item, const QString& author, const QString& project, const QString& nifFolder);
	NifItem* findChildByName(NifItem* parent, const QString& name);
	QString sanitizeFileName(const QString& input);

	QModelIndex cast(NifModel* nif, const QModelIndex& index) override final;
};

QString spResourceCopy::sanitizeFileName(const QString& input)
{
	// Convert to lowercase
	QString sanitized = input.toLower();

	// Remove periods, spaces, and slashes
	sanitized.remove(QRegularExpression("[\\.\\s/\\\\]"));

	// Replace special filesystem characters with an underscore
	QMap<QChar, QChar> specialChars = {
		{'<', '_'}, {'>', '_'}, {':', '_'}, {'"', '_'},
		{'|', '_'}, {'?', '_'}, {'*', '_'}
	};
	for (auto it = specialChars.constBegin(); it != specialChars.constEnd(); ++it) {
		sanitized.replace(it.key(), it.value());
	}

	// Convert Unicode characters to ASCII representation
	QString result;
	for (int i = 0; i < sanitized.size(); ++i) {
		QChar c = sanitized.at(i);
		if (c.unicode() < 128) {
			result.append(c);
		}
		else {
			// Decompose the Unicode character to its ASCII representation
			QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, sanitized);
			finder.setPosition(i);
			while (finder.position() < sanitized.size()) {
				int nextBoundary = finder.toNextBoundary();
				QString chunk = sanitized.mid(finder.position(), nextBoundary - finder.position());
				QString asciiChunk;
				for (const QChar& subChar : chunk) {
					if (subChar.unicode() < 128) {
						asciiChunk.append(subChar);
					}
				}
				result.append(asciiChunk);
				finder.setPosition(nextBoundary);
			}
			if (result.isEmpty() || result.back() != '_') {
				result.append('_');
			}
		}
	}

	// Remove any remaining non-ASCII characters
	result.remove(QRegularExpression("[^\\x20-\\x7E]"));

	return result;
}

NifItem* spResourceCopy::findChildByName(NifItem* parent, const QString& name)
{
	for (int i = 0; i < parent->childCount(); i++) {
		NifItem* child = parent->child(i);
		if (child && child->name() == name) {
			return child;
		}
	}
	return nullptr;
}

void spResourceCopy::copyPaths(NifModel* nif, NifItem* item, const QString& author, const QString& project, const QString& nifFolder)
{
	if (item && item->name() == "BSGeometry") {
		QString objectName = nif->get<QString>(item, "Name");
		NifItem* meshArrayItems = findChildByName(item, "Meshes");

		if (meshArrayItems) {
			for (int i = 0; i < meshArrayItems->childCount(); i++) {
				NifItem* meshArrayItem = meshArrayItems->child(i);
				if (!nif->get<bool>(meshArrayItem, "Has Mesh")) {
					continue;
				}
				NifItem* mesh = findChildByName(meshArrayItem, "Mesh");
				if (mesh) {
					// The nif field doesn't include the .mesh extension, and always uses a forward slash
					QString meshPath = nif->get<QString>(mesh, "Mesh Path");
					// Not using QDir because file as stored uses forward slashes and no extension
					QString newMeshPath = author + "/" + project + "/" + sanitizeFileName(objectName) + "_lod" + QString::number(i + 1);

					// Convert paths to absolute using nifFolder as root
					QString oldPath = QDir(nifFolder).filePath("geometries/" + meshPath + ".mesh");
					QString newPath = QDir(nifFolder).filePath("geometries/" + newMeshPath + ".mesh");

					// Create the directory for the new path if it doesn't exist
					QDir newDir = QFileInfo(newPath).absoluteDir();
					newDir.mkpath(newDir.absolutePath());

					// Copy the file (platform-independent with the slashes)
					if ( !QFile::copy(QDir::fromNativeSeparators(oldPath), QDir::fromNativeSeparators(newPath)) ) {
						QByteArray	meshData;
						if ( nif->getResourceFile(meshData, meshPath, "geometries/", ".mesh") ) {
							QFile	newFile( QDir::fromNativeSeparators(newPath) );
							if ( newFile.open(QIODevice::WriteOnly) )
								(void) newFile.write( meshData );
						}
					}

					// Update the value in the nif
					findChildByName(mesh,"Mesh Path")->setValueFromString(newMeshPath);
				}
			}
		}
		// BSGeometri are leaf structures so no need to process children
	}
	else {
		// Process children
		for (int i = 0; i < item->childCount(); i++) {
			if (item->child(i)) {
				copyPaths(nif, item->child(i), author, project, nifFolder);
			}
		}
	}
}

QModelIndex spResourceCopy::cast(NifModel* nif, const QModelIndex& index)
{
	if (!nif)
		return index;

	QDialog dlg;
	QLabel* lb = new QLabel(&dlg);
	lb->setAlignment(Qt::AlignCenter);
	lb->setText(tr("Copy and rename meshes to this format:\ngeometries/author/project/objectname_lod#"));

	QLabel* lb1 = new QLabel(&dlg);
	lb1->setText(tr("Author Prefix:"));
	QLineEdit* le1 = new QLineEdit(&dlg);
	le1->setFocus();

	QLabel* lb2 = new QLabel(&dlg);
	lb2->setText(tr("Project Name:"));
	QLineEdit* le2 = new QLineEdit(&dlg);
	le2->setFocus();

	QPushButton* bo = new QPushButton(tr("Ok"), &dlg);
	QObject::connect(bo, &QPushButton::clicked, &dlg, &QDialog::accept);

	QPushButton* bc = new QPushButton(tr("Cancel"), &dlg);
	QObject::connect(bc, &QPushButton::clicked, &dlg, &QDialog::reject);

	QGridLayout* grid = new QGridLayout;
	dlg.setLayout(grid);
	grid->addWidget(lb, 0, 0, 1, 2);
	grid->addWidget(lb1, 1, 0, 1, 2);
	grid->addWidget(le1, 2, 0, 1, 2);
	grid->addWidget(lb2, 3, 0, 1, 2);
	grid->addWidget(le2, 4, 0, 1, 2);
	grid->addWidget(bo, 5, 0, 1, 1);
	grid->addWidget(bc, 5, 1, 1, 1);

	if (dlg.exec() != QDialog::Accepted) {
		return index;
	}

	// Strip off any leading or trailing spaces and either forward or back slashes (leading or trailing)
	QString authorPrefix = sanitizeFileName(le1->text().trimmed().remove(QRegularExpression("^[\\\\/]+|[\\\\/]+$")));
	QString projectName = sanitizeFileName(le2->text().trimmed().remove(QRegularExpression("^[\\\\/]+|[\\\\/]+$")));

	for (int b = 0; b < nif->getBlockCount(); b++) {
		NifItem* item = nif->getBlockItem(quint32(b));
		if (item)
			copyPaths(nif, item, authorPrefix, projectName, nif->getFolder());
	}

	return index;
}

REGISTER_SPELL( spResourceCopy )
