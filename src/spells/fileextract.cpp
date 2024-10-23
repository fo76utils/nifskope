#include "spellbook.h"

#include <QDialog>
#include <QCheckBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QIODevice>
#include <QBuffer>
#include <QCryptographicHash>

#include "libfo76utils/src/common.hpp"
#include "libfo76utils/src/filebuf.hpp"
#include "libfo76utils/src/material.hpp"
#include "model/nifmodel.h"
#include "io/nifstream.h"
#include "nifskope.h"

#ifdef Q_OS_WIN32
#  include <direct.h>
#else
#  include <sys/stat.h>
#endif

// Brief description is deliberately not autolinked to class Spell
/*! \file fileextract.cpp
 * \brief Resource file extraction spell (spResourceFileExtract)
 *
 * All classes here inherit from the Spell class.
 */

//! Extract a resource file
class spResourceFileExtract final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Extract File" ); }
	QString page() const override final { return Spell::tr( "" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool constant() const override final { return true; }
	bool instant() const override final { return true; }

	static bool is_Applicable( const NifModel * nif, const NifItem * item )
	{
		NifValue::Type	vt = item->valueType();
		if ( vt != NifValue::tStringIndex && vt != NifValue::tSizedString && vt != NifValue::tSizedString16 ) {
			if ( !( nif->checkVersion( 0x14010003, 0 ) && ( vt == NifValue::tString || vt == NifValue::tFilePath ) ) )
				return false;
		}
		do {
			if ( item->parent() && nif && nif->getBSVersion() >= 130 ) {
				if ( item->name() == "Name" && ( item->parent()->name() == "BSLightingShaderProperty" || item->parent()->name() == "BSEffectShaderProperty" ) )
					break;		// Fallout 4, 76 or Starfield material
			}
			if ( item->parent() && item->parent()->name() == "Textures" )
				break;
			if ( item->name() == "Path" || item->name() == "Mesh Path" || item->name().startsWith( "Texture " ) )
				break;
			return false;
		} while ( false );
		return !( nif->resolveString( item ).isEmpty() );
	}

	static std::string getNifItemFilePath( NifModel * nif, const NifItem * item );
	static std::string getOutputDirectory( const NifModel * nif = nullptr );
	static void writeFileWithPath( const std::string & fileName, const char * buf, qsizetype bufSize );

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		const NifItem * item = nif->getItem( index );
		return ( item && is_Applicable( nif, item ) );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

std::string spResourceFileExtract::getNifItemFilePath( NifModel * nif, const NifItem * item )
{
	const char *	archiveFolder = nullptr;
	const char *	extension = nullptr;

	quint32	bsVersion = nif->getBSVersion();
	if ( item->parent() && bsVersion >= 130 && item->name() == "Name" ) {
		if ( item->parent()->name() == "BSLightingShaderProperty" ) {
			archiveFolder = "materials/";
			extension = ( bsVersion < 170 ? ".bgsm" : ".mat" );
		} else if ( item->parent()->name() == "BSEffectShaderProperty" ) {
			archiveFolder = "materials/";
			extension = ( bsVersion < 170 ? ".bgem" : ".mat" );
		}
	} else if ( ( item->parent() && item->parent()->name() == "Textures" ) || item->name().contains( "Texture" ) || ( bsVersion >= 170 && item->name() == "Path" ) ) {
		archiveFolder = "textures/";
		extension = ".dds";
	} else if ( bsVersion >= 170 && item->name() == "Mesh Path" ) {
		archiveFolder = "geometries/";
		extension = ".mesh";
	}

	QString	filePath( nif->resolveString( item ) );
	if ( filePath.isEmpty() )
		return std::string();
	return Game::GameManager::get_full_path( filePath, archiveFolder, extension );
}

std::string spResourceFileExtract::getOutputDirectory( const NifModel * nif )
{
	QSettings	settings;
	QString	key = QString( "Spells//Extract File/Last File Path" );
	QString	dstPath( settings.value( key ).toString() );
	if ( !( nif && nif->getBatchProcessingMode() ) ) {
		QFileDialog	dialog( nullptr, "Select Export Data Path" );
		dialog.setFileMode( QFileDialog::Directory );
		if ( !dstPath.isEmpty() )
			dialog.setDirectory( dstPath );
		if ( !dialog.exec() )
			return std::string();
		dstPath = dialog.selectedFiles().at( 0 );
		if ( dstPath.isEmpty() )
			return std::string();
		settings.setValue( key, QVariant(dstPath) );
	} else if ( dstPath.isEmpty() ) {
		return std::string();
	}

	std::string	fullPath( dstPath.replace( QChar('\\'), QChar('/') ).toStdString() );
	if ( !fullPath.ends_with( '/' ) )
		fullPath += '/';
	return fullPath;
}

void spResourceFileExtract::writeFileWithPath( const std::string & fileName, const char * buf, qsizetype bufSize )
{
	if ( bufSize < 0 )
		return;
	OutputFile *	f = nullptr;
	try {
		f = new OutputFile( fileName.c_str(), 0 );
	} catch ( ... ) {
		std::string	pathName;
		size_t	pathOffs = 0;
		while (true) {
			pathName = fileName;
			pathOffs = pathName.find( '/', pathOffs );
			if ( pathOffs == std::string::npos )
				break;
			pathName.resize( pathOffs );
			pathOffs++;
#ifdef Q_OS_WIN32
			(void) _mkdir( pathName.c_str() );
#else
			(void) mkdir( pathName.c_str(), 0755 );
#endif
		}
		f = new OutputFile( fileName.c_str(), 0 );
	}

	try {
		f->writeData( buf, size_t(bufSize) );
	}
	catch ( ... ) {
		delete f;
		throw;
	}
	delete f;
}

QModelIndex spResourceFileExtract::cast( NifModel * nif, const QModelIndex & index )
{
	if ( !nif )
		return index;

	const NifItem * item = nif->getItem( index );
	if ( !item )
		return index;

	std::string	filePath( getNifItemFilePath( nif, item ) );
	if ( filePath.empty() )
		return index;

	std::string	matFileData;
	try {
		if ( nif->getBSVersion() >= 170 && filePath.ends_with( ".mat" ) && filePath.starts_with( "materials/" ) ) {
			CE2MaterialDB *	materials = nif->getCE2Materials();
			if ( materials ) {
				(void) materials->loadMaterial( filePath );
				materials->getJSONMaterial( matFileData, filePath );
			}
			if ( matFileData.empty() )
				return index;
		} else if ( nif->findResourceFile( QString::fromStdString( filePath ), nullptr, nullptr ).isEmpty() ) {
			return index;
		}

		std::string	fullPath( getOutputDirectory( nif ) );
		if ( fullPath.empty() )
			return index;
		fullPath += filePath;

		if ( !matFileData.empty() ) {
			matFileData += '\n';
			writeFileWithPath( fullPath, matFileData.c_str(), qsizetype(matFileData.length()) );
		} else {
			QByteArray	fileData;
			if ( nif->getResourceFile( fileData, filePath ) )
				writeFileWithPath( fullPath, fileData.data(), fileData.size() );
		}
	} catch ( std::exception & e ) {
		QMessageBox::critical( nullptr, "NifSkope error", QString("Error extracting file: %1" ).arg( e.what() ) );
	}
	return index;
}

REGISTER_SPELL( spResourceFileExtract )

//! Extract all resource files
class spExtractAllResources final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Extract Resource Files" ); }
	QString page() const override final { return Spell::tr( "" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool constant() const override final { return true; }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return ( !index.isValid() && nif );
	}

	static void findPaths( std::set< std::string > & fileSet, NifModel * nif, const NifItem * item );
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

void spExtractAllResources::findPaths( std::set< std::string > & fileSet, NifModel * nif, const NifItem * item )
{
	if ( spResourceFileExtract::is_Applicable( nif, item ) ) {
		std::string	filePath( spResourceFileExtract::getNifItemFilePath( nif, item ) );
		if ( !filePath.empty() )
			fileSet.insert( filePath );
	}

	for ( int i = 0; i < item->childCount(); i++ ) {
		if ( item->child( i ) )
			findPaths( fileSet, nif, item->child( i ) );
	}
}

QModelIndex spExtractAllResources::cast( NifModel * nif, const QModelIndex & index )
{
	if ( !nif )
		return index;

	std::set< std::string >	fileSet;
	for ( int b = 0; b < nif->getBlockCount(); b++ ) {
		const NifItem * item = nif->getBlockItem( qint32(b) );
		if ( item )
			findPaths( fileSet, nif, item );
	}
	if ( fileSet.begin() == fileSet.end() )
		return index;

	std::string	dstPath( spResourceFileExtract::getOutputDirectory( nif ) );
	if ( dstPath.empty() )
		return index;

	std::string	matFileData;
	std::string	fullPath;
	QByteArray	fileData;
	try {
		for ( std::set< std::string >::const_iterator i = fileSet.begin(); i != fileSet.end(); i++ ) {
			matFileData.clear();
			if ( nif->getBSVersion() >= 170 && i->ends_with( ".mat" ) && i->starts_with( "materials/" ) ) {
				CE2MaterialDB *	materials = nif->getCE2Materials();
				if ( materials ) {
					(void) materials->loadMaterial( *i );
					materials->getJSONMaterial( matFileData, *i );
				}
				if ( matFileData.empty() )
					continue;
			} else if ( nif->findResourceFile( QString::fromStdString( *i ), nullptr, nullptr ).isEmpty() ) {
				continue;
			}

			fullPath = dstPath;
			fullPath += *i;
			if ( !matFileData.empty() ) {
				matFileData += '\n';
				spResourceFileExtract::writeFileWithPath( fullPath, matFileData.c_str(), qsizetype(matFileData.length()) );
			} else if ( nif->getResourceFile( fileData, *i ) ) {
				spResourceFileExtract::writeFileWithPath( fullPath, fileData.data(), fileData.size() );
			}
		}
	} catch ( std::exception & e ) {
		QMessageBox::critical( nullptr, "NifSkope error", QString("Error extracting file: %1" ).arg( e.what() ) );
	}
	return index;
}

REGISTER_SPELL( spExtractAllResources )

//! Extract all Starfield materials
class spExtractAllMaterials final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Extract All..." ); }
	QString page() const override final { return Spell::tr( "Material" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool constant() const override final { return true; }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return ( !index.isValid() && nif && nif->getBSVersion() >= 170 );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

QModelIndex spExtractAllMaterials::cast( NifModel * nif, const QModelIndex & index )
{
	if ( !nif )
		return index;

	CE2MaterialDB *	materials = nif->getCE2Materials();
	if ( !materials )
		return index;

	AllocBuffers	matPathBuf;
	std::set< std::string_view >	fileSet;
	materials->getMaterialList( fileSet, matPathBuf );
	if ( fileSet.empty() )
		return index;

	std::string	dstPath( spResourceFileExtract::getOutputDirectory( nif ) );
	if ( dstPath.empty() )
		return index;

	QDialog	dlg;
	QLabel *	lb = new QLabel( &dlg );
	lb->setText( Spell::tr( "Extracting %1 materials..." ).arg( fileSet.size() ) );
	QProgressBar *	pb = new QProgressBar( &dlg );
	pb->setMinimum( 0 );
	pb->setMaximum( int( fileSet.size() ) );
	QPushButton *	cb = new QPushButton( Spell::tr( "Cancel" ), &dlg );
	QGridLayout *	grid = new QGridLayout;
	dlg.setLayout( grid );
	grid->addWidget( lb, 0, 0, 1, 3 );
	grid->addWidget( pb, 1, 0, 1, 3 );
	grid->addWidget( cb, 2, 1, 1, 1 );
	QObject::connect( cb, &QPushButton::clicked, &dlg, &QDialog::reject );
	dlg.setModal( true );
	dlg.setResult( QDialog::Accepted );
	dlg.show();

	std::string	matFileData;
	std::string	fullPath;
	try {
		int	n = 0;
		for ( const auto & i : fileSet ) {
			QCoreApplication::processEvents();
			if ( dlg.result() == QDialog::Rejected )
				break;
			matFileData.clear();
			try {
				(void) materials->loadMaterial( i );
				materials->getJSONMaterial( matFileData, i );
			} catch ( FO76UtilsError & e ) {
				QMessageBox::critical( nullptr, "NifSkope error", QString( "Error loading material '%1': %2" ).arg( QLatin1String( i.data(), qsizetype(i.length()) ) ).arg( e.what() ) );
			}
			if ( !matFileData.empty() ) {
				matFileData += '\n';
				fullPath = dstPath;
				fullPath += i;
				spResourceFileExtract::writeFileWithPath( fullPath, matFileData.c_str(), qsizetype(matFileData.length()) );
			}
			n++;
			pb->setValue( n );
		}
	} catch ( std::exception & e ) {
		QMessageBox::critical( nullptr, "NifSkope error", QString("Error extracting file: %1" ).arg( e.what() ) );
	}
	return index;
}

REGISTER_SPELL( spExtractAllMaterials )

//! Convert Starfield BSGeometry block(s) to use external geometry data
class spMeshFileExport final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Convert to External Geometry" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( !( nif && nif->getBSVersion() >= 170 ) )
			return false;
		const NifItem *	item = nif->getItem( index, false );
		if ( !item )
			return true;
		return ( item->name() == "BSGeometry" && ( nif->get<quint32>(item, "Flags") & 0x0200 ) != 0 );
	}

	bool processItem( NifModel * nif, NifItem * item, const std::string & outputDirectory, const QString & meshDir );
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

bool spMeshFileExport::processItem(
	NifModel * nif, NifItem * item, const std::string & outputDirectory, const QString & meshDir )
{
	quint32	flags;
	if ( !( item && item->name() == "BSGeometry" && ( (flags = nif->get<quint32>(item, "Flags")) & 0x0200 ) != 0 ) )
		return false;

	QString	meshPaths[4];
	bool	haveMeshes = false;

	auto	meshesIndex = nif->getIndex( item, "Meshes" );
	if ( meshesIndex.isValid() ) {
		for ( int l = 0; l < 4; l++ ) {
			auto	meshIndex = nif->getIndex( meshesIndex, l );
			if ( !( meshIndex.isValid() && nif->get<bool>(meshIndex, "Has Mesh") ) )
				continue;
			auto	meshData = nif->getIndex( nif->getIndex( meshIndex, "Mesh" ), "Mesh Data" );
			if ( !meshData.isValid() )
				continue;
			haveMeshes = true;

			QByteArray	meshBuf;
			{
				QBuffer	tmpBuf( &meshBuf );
				tmpBuf.open( QIODevice::WriteOnly );
				NifOStream	nifStream( nif, &tmpBuf );
				nif->saveItem( nif->getItem( meshData, false ), nifStream );
			}
			if ( !( nif->get<quint32>( meshData, "Num Meshlets" ) | nif->get<quint32>( meshData, "Num Cull Data" ) ) )
				meshBuf.chop( 8 );	// end of file after LODs if there are no meshlets

			QCryptographicHash	h( QCryptographicHash::Sha1 );
			h.addData( meshBuf );
			meshPaths[l] = h.result().toHex();
			if ( meshDir.isEmpty() )
				meshPaths[l].insert( 20, QChar('\\') );
			else
				meshPaths[l].insert( 0, meshDir );

			std::string	fullPath( outputDirectory );
			fullPath += Game::GameManager::get_full_path( meshPaths[l], "geometries/", ".mesh" );
			try {
				spResourceFileExtract::writeFileWithPath( fullPath, meshBuf.data(), meshBuf.size() );
			} catch ( std::exception & e ) {
				QMessageBox::critical( nullptr, "NifSkope error", QString("Error extracting file: %1" ).arg( e.what() ) );
			}
		}
	}

	item->invalidateVersionCondition();
	item->invalidateCondition();
	nif->set<quint32>( item, "Flags", flags & ~0x0200U );

	meshesIndex = nif->getIndex( item, "Meshes" );
	for ( int l = 0; l < 4; l++ ) {
		auto	meshIndex = nif->getIndex( meshesIndex, l );
		if ( !( meshIndex.isValid() && nif->get<bool>(meshIndex, "Has Mesh") ) )
			continue;
		nif->set<QString>( nif->getIndex( meshIndex, "Mesh" ), "Mesh Path", meshPaths[l] );
	}

	return haveMeshes;
}

QModelIndex spMeshFileExport::cast( NifModel * nif, const QModelIndex & index )
{
	if ( !( nif && nif->getBSVersion() >= 170 ) )
		return index;

	NifItem *	item = nif->getItem( index, false );
	if ( item && !( item->name() == "BSGeometry" && (nif->get<quint32>(item, "Flags") & 0x0200) != 0 ) )
		return index;

	std::string	outputDirectory( spResourceFileExtract::getOutputDirectory( nif ) );
	if ( outputDirectory.empty() )
		return index;

	QString	meshDir;
	{
		QSettings	settings;
		meshDir = settings.value( "Settings/Nif/Mesh Export Dir", QString() ).toString().trimmed().toLower();
	}
	meshDir.replace( QChar('/'), QChar('\\') );
	while ( meshDir.endsWith( QChar('\\') ) )
		meshDir.chop( 1 );
	while ( meshDir.startsWith( QChar('\\') ) )
		meshDir.remove( 0, 1 );
	if ( !meshDir.isEmpty() )
		meshDir.append( QChar('\\') );

	bool	meshesConverted = false;
	if ( item ) {
		meshesConverted = processItem( nif, item, outputDirectory, meshDir );
	} else {
		for ( int b = 0; b < nif->getBlockCount(); b++ )
			meshesConverted |= processItem( nif, nif->getBlockItem( qint32(b) ), outputDirectory, meshDir );
	}
	if ( meshesConverted && !nif->getBatchProcessingMode() )
		Game::GameManager::close_resources();

	return index;
}

REGISTER_SPELL( spMeshFileExport )

//! Convert Starfield BSGeometry block(s) to use internal geometry data
class spMeshFileImport final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Convert to Internal Geometry" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( !( nif && nif->getBSVersion() >= 170 ) )
			return false;
		const NifItem *	item = nif->getItem( index, false );
		if ( !item )
			return true;
		return ( item->name() == "BSGeometry" && ( nif->get<quint32>(item, "Flags") & 0x0200 ) == 0 );
	}

	static bool processItem( NifModel * nif, NifItem * item );
	static bool processAllItems( NifModel * nif );
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

bool spMeshFileImport::processItem( NifModel * nif, NifItem * item )
{
	quint32	flags;
	if ( !( item && item->name() == "BSGeometry" && ( (flags = nif->get<quint32>(item, "Flags")) & 0x0200 ) == 0 ) )
		return false;

	QByteArray	meshData[4];

	auto	meshesIndex = nif->getIndex( item, "Meshes" );
	if ( meshesIndex.isValid() ) {
		for ( int l = 0; l < 4; l++ ) {
			auto	meshIndex = nif->getIndex( meshesIndex, l );
			if ( !( meshIndex.isValid() && nif->get<bool>(meshIndex, "Has Mesh") ) )
				continue;
			QString	meshPath = nif->get<QString>( nif->getIndex( meshIndex, "Mesh" ), "Mesh Path" );
			if ( meshPath.isEmpty() )
				continue;
			if ( !nif->getResourceFile( meshData[l], meshPath, "geometries/", ".mesh" ) ) {
				if ( nif->getBatchProcessingMode() )
					throw FO76UtilsError( "failed to load mesh file '%s'", meshPath.toStdString().c_str() );
				else
					QMessageBox::critical( nullptr, "NifSkope error", QString("Failed to load mesh file '%1'" ).arg( meshPath ) );
				return false;
			}
		}
	}

	item->invalidateVersionCondition();
	item->invalidateCondition();
	nif->set<quint32>( item, "Flags", flags | 0x0200U );

	meshesIndex = nif->getIndex( item, "Meshes" );
	for ( int l = 0; l < 4; l++ ) {
		auto	meshIndex = nif->getIndex( meshesIndex, l );
		if ( !( meshIndex.isValid() && nif->get<bool>(meshIndex, "Has Mesh") ) )
			continue;

		NifItem *	meshItem = nif->getItem( nif->getIndex( meshIndex, "Mesh" ), "Mesh Data" );
		if ( !meshItem )
			continue;

		QBuffer	meshBuf;
		meshBuf.setData( meshData[l] );
		meshBuf.open( QIODevice::ReadOnly );
		{
			NifIStream	nifStream( nif, &meshBuf );
			nif->loadItem( meshItem, nifStream );
		}
	}

	return true;
}

bool spMeshFileImport::processAllItems( NifModel * nif )
{
	bool	r = false;
	for ( int b = 0; b < nif->getBlockCount(); b++ )
		r = r | processItem( nif, nif->getBlockItem( qint32(b) ) );
	return r;
}

QModelIndex spMeshFileImport::cast( NifModel * nif, const QModelIndex & index )
{
	if ( !( nif && nif->getBSVersion() >= 170 ) )
		return index;

	NifItem *	item = nif->getItem( index, false );
	if ( item && !( item->name() == "BSGeometry" && (nif->get<quint32>(item, "Flags") & 0x0200) == 0 ) )
		return index;

	if ( item )
		processItem( nif, item );
	else
		processAllItems( nif );

	return index;
}

REGISTER_SPELL( spMeshFileImport )

//! Batch process multiple NIF files
class spBatchProcessFiles final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Process Multiple NIF Files" ); }
	QString page() const override final { return Spell::tr( "Batch" ); }
	QIcon icon() const override final
	{
		return QIcon();
	}
	bool constant() const override final { return true; }
	bool instant() const override final { return true; }

	bool isApplicable( [[maybe_unused]] const NifModel * nif, const QModelIndex & index ) override final
	{
		return !index.isValid();
	}

	enum {
		spellFlagInternalGeom = 1,
		spellFlagRemoveUnusedStrings = 2,
		spellFlagLODGen = 4,
		spellFlagTangentSpace = 8,
		spellFlagMeshlets = 16,
		spellFlagUpdateBounds = 32,
		spellFlagExternalGeom = 64
	};
	static bool processFile( NifModel * nif, void * p );
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

class spRemoveUnusedStrings
{
public:
	static QModelIndex cast_Static( NifModel * nif, const QModelIndex & index );
};

class spSimplifySFMesh
{
public:
	static QModelIndex cast_Static( NifModel * nif, const QModelIndex & index );
};

class spAddAllTangentSpaces
{
public:
	static QModelIndex cast_Static( NifModel * nif, const QModelIndex & index );
};

class spGenerateMeshlets
{
public:
	static QModelIndex cast_Static( NifModel * nif, const QModelIndex & index );
};

class spUpdateAllBounds
{
public:
	static QModelIndex cast_Static( NifModel * nif, const QModelIndex & index );
};

bool spBatchProcessFiles::processFile( NifModel * nif, void * p )
{
	int	spellMask = *( reinterpret_cast< int * >( p ) );
	bool	fileChanged = false;

	if ( ( spellMask & spellFlagInternalGeom ) && nif->getBSVersion() >= 170 ) {
		spMeshFileImport::processAllItems( nif );
		fileChanged = true;
	}

	if ( spellMask & spellFlagRemoveUnusedStrings ) {
		spRemoveUnusedStrings::cast_Static( nif, QModelIndex() );
		fileChanged = true;
	}

	if ( ( spellMask & spellFlagLODGen ) && nif->getBSVersion() >= 170 ) {
		spSimplifySFMesh::cast_Static( nif, QModelIndex() );
		fileChanged = true;
	}

	if ( spellMask & spellFlagTangentSpace ) {
		spAddAllTangentSpaces::cast_Static( nif, QModelIndex() );
		fileChanged = true;
	}

	if ( ( spellMask & spellFlagMeshlets ) && nif->getBSVersion() >= 170 ) {
		spGenerateMeshlets::cast_Static( nif, QModelIndex() );
		fileChanged = true;
	}

	if ( spellMask & spellFlagUpdateBounds ) {
		spUpdateAllBounds::cast_Static( nif, QModelIndex() );
		fileChanged = true;
	}

	if ( ( spellMask & spellFlagExternalGeom ) && nif->getBSVersion() >= 170 ) {
		spMeshFileExport	sp;
		sp.cast( nif, QModelIndex() );
		fileChanged = true;
	}

	return fileChanged;
}

QModelIndex spBatchProcessFiles::cast( [[maybe_unused]] NifModel * nif, const QModelIndex & index )
{
	if ( index.isValid() )
		return index;

	int	spellMask = 0;
	{
		QDialog	dlg;
		QLabel *	lb = new QLabel( &dlg );
		lb->setText( "Batch process multiple models, overwriting the original NIF files" );
		QLabel *	lb2 = new QLabel( "Select spells to be cast, in the order listed:", &dlg );
		QCheckBox *	checkInternalGeom = new QCheckBox( "Convert to Internal Geometry", &dlg );
		QCheckBox *	checkRemoveUnusedStrings = new QCheckBox( "Remove Unused Strings", &dlg );
		QCheckBox *	checkLODGen = new QCheckBox( "Generate LODs", &dlg );
		QCheckBox *	checkTangentSpace = new QCheckBox( "Add Tangent Spaces and Update", &dlg );
		QCheckBox *	checkMeshlets = new QCheckBox( "Generate Meshlets and Update Bounds", &dlg );
		QCheckBox *	checkUpdateBounds = new QCheckBox( "Update Bounds", &dlg );
		QCheckBox *	checkExternalGeom = new QCheckBox( "Convert to External Geometry", &dlg );
		QPushButton *	okButton = new QPushButton( "OK", &dlg );
		QPushButton *	cancelButton = new QPushButton( "Cancel", &dlg );

		QGridLayout *	grid = new QGridLayout;
		dlg.setLayout( grid );
		grid->addWidget( lb, 0, 0, 1, 5 );
		grid->addWidget( new QLabel( "", &dlg ), 1, 0, 1, 5 );
		grid->addWidget( lb2, 2, 0, 1, 5 );
		grid->addWidget( checkInternalGeom, 3, 0, 1, 5 );
		grid->addWidget( checkRemoveUnusedStrings, 4, 0, 1, 5 );
		grid->addWidget( checkLODGen, 5, 0, 1, 5 );
		grid->addWidget( checkTangentSpace, 6, 0, 1, 5 );
		grid->addWidget( checkMeshlets, 7, 0, 1, 5 );
		grid->addWidget( checkUpdateBounds, 8, 0, 1, 5 );
		grid->addWidget( checkExternalGeom, 9, 0, 1, 5 );
		grid->addWidget( new QLabel( "", &dlg ), 10, 0, 1, 5 );
		grid->addWidget( okButton, 11, 1, 1, 1 );
		grid->addWidget( cancelButton, 11, 3, 1, 1 );

		QObject::connect( okButton, &QPushButton::clicked, &dlg, &QDialog::accept );
		QObject::connect( cancelButton, &QPushButton::clicked, &dlg, &QDialog::reject );

		if ( dlg.exec() != QDialog::Accepted )
			return index;

		if ( checkInternalGeom->isChecked() )
			spellMask = spellFlagInternalGeom;
		if ( checkRemoveUnusedStrings->isChecked() )
			spellMask = spellMask | spellFlagRemoveUnusedStrings;
		if ( checkLODGen->isChecked() )
			spellMask = spellMask | spellFlagLODGen;
		if ( checkTangentSpace->isChecked() )
			spellMask = spellMask | spellFlagTangentSpace;
		if ( checkMeshlets->isChecked() )
			spellMask = spellMask | spellFlagMeshlets;
		if ( checkUpdateBounds->isChecked() )
			spellMask = spellMask | spellFlagUpdateBounds;
		if ( checkExternalGeom->isChecked() )
			spellMask = spellMask | spellFlagExternalGeom;
		if ( !spellMask )
			return index;
	}

	QStringList	fileList;
	{
		QFileDialog	fd( nullptr, "Select NIF Files to Process" );
		fd.setFileMode( QFileDialog::ExistingFiles );
		fd.setNameFilter( "NIF files (*.nif)" );
		if ( fd.exec() )
			fileList = fd.selectedFiles();
	}
	if ( fileList.isEmpty() )
		return index;
	if ( spellMask & spellFlagExternalGeom )
		(void) spResourceFileExtract::getOutputDirectory();

	NifSkope *	w = dynamic_cast< NifSkope * >( nif->getWindow() );
	if ( w ) {
		w->batchProcessFiles( fileList, &processFile, &spellMask );
		if ( spellMask & spellFlagExternalGeom )
			Game::GameManager::close_resources();
	}

	return index;
}

REGISTER_SPELL( spBatchProcessFiles )

