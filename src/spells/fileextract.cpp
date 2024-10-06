#include "spellbook.h"

#include <QDialog>
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
	static std::string getOutputDirectory();
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

std::string spResourceFileExtract::getOutputDirectory()
{
	QSettings	settings;
	QString	key = QString( "Spells//Extract File/Last File Path" );
	QString	dstPath( settings.value( key ).toString() );
	{
		QFileDialog	dialog;
		dialog.setFileMode( QFileDialog::Directory );
		if ( !dstPath.isEmpty() )
			dialog.setDirectory( dstPath );
		if ( !dialog.exec() )
			return std::string();
		dstPath = dialog.selectedFiles().at( 0 );
	}
	if ( dstPath.isEmpty() )
		return std::string();
	settings.setValue( key, QVariant(dstPath) );

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

		std::string	fullPath( getOutputDirectory() );
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

	std::string	dstPath( spResourceFileExtract::getOutputDirectory() );
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

	std::string	dstPath( spResourceFileExtract::getOutputDirectory() );
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

	bool processItem( NifModel * nif, NifItem * item, const std::string & outputDirectory );
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

bool spMeshFileExport::processItem( NifModel * nif, NifItem * item, const std::string & outputDirectory )
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

			QBuffer	meshBuf;
			meshBuf.open( QIODevice::WriteOnly );
			{
				NifOStream	nifStream( nif, &meshBuf );
				nif->saveItem( nif->getItem( meshData, false ), nifStream );
			}

			QCryptographicHash	h( QCryptographicHash::Sha1 );
			h.addData( meshBuf.data() );
			meshPaths[l] = h.result().toHex();
			meshPaths[l].insert( 20, QChar('\\') );

			std::string	fullPath( outputDirectory );
			fullPath += Game::GameManager::get_full_path( meshPaths[l], "geometries/", ".mesh" );
			try {
				spResourceFileExtract::writeFileWithPath( fullPath, meshBuf.data().data(), meshBuf.data().size() );
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

	std::string	outputDirectory( spResourceFileExtract::getOutputDirectory() );
	if ( outputDirectory.empty() )
		return index;

	bool	meshesConverted = false;
	if ( item ) {
		meshesConverted = processItem( nif, item, outputDirectory );
	} else {
		for ( int b = 0; b < nif->getBlockCount(); b++ )
			meshesConverted |= processItem( nif, nif->getBlockItem( qint32(b) ), outputDirectory );
	}
	if ( meshesConverted )
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

