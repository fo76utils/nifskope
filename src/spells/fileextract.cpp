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
	bool constant() const override final { return true; }

	static bool is_Applicable( const NifModel * nif, const NifItem * item )
	{
		NifValue::Type	vt = item->valueType();
		if ( vt != NifValue::tStringIndex && vt != NifValue::tSizedString && vt != NifValue::tSizedString16 )
			return false;
		do {
			if ( item->parent() && nif && nif->getBSVersion() >= 130 ) {
				if ( item->hasName( "Name" )
					&& ( item->parent()->hasName( "BSLightingShaderProperty" )
						|| item->parent()->hasName( "BSEffectShaderProperty" ) ) ) {
					break;		// Fallout 4, 76 or Starfield material
				}
			}
			if ( item->parent() && item->parent()->hasName( "Textures" ) )
				break;
			if ( item->hasName( "Path" ) || item->hasName( "Mesh Path" ) || item->hasName( "File Name" )
				|| item->name().contains( QLatin1StringView( "Texture" ) ) ) {
				break;
			}
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
	if ( item->parent() && bsVersion >= 130 && item->hasName( "Name" ) ) {
		if ( item->parent()->hasName( "BSLightingShaderProperty" ) ) {
			archiveFolder = "materials/";
			extension = ( bsVersion < 170 ? ".bgsm" : ".mat" );
		} else if ( item->parent()->hasName( "BSEffectShaderProperty" ) ) {
			archiveFolder = "materials/";
			extension = ( bsVersion < 170 ? ".bgem" : ".mat" );
		}
	} else if ( ( item->parent()
					&& ( item->parent()->hasName( "Textures" ) || item->parent()->hasName( "NiSourceTexture" ) ) )
				|| item->name().contains( QLatin1StringView( "Texture" ) )
				|| ( bsVersion >= 170 && item->hasName( "Path" ) ) ) {
		archiveFolder = "textures/";
		extension = ".dds";
	} else if ( bsVersion >= 170 && item->hasName( "Mesh Path" ) ) {
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
		QMessageBox::critical( nullptr, "NifSkope error", QString( "Error extracting file: %1" ).arg( e.what() ) );
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
	bool constant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return ( !index.isValid() && nif );
	}

	static void addPath( std::set< std::string > & fileSet, NifModel * nif, const std::string & filePath );
	static void findPaths( std::set< std::string > & fileSet, NifModel * nif, const NifItem * item );
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

void spExtractAllResources::addPath( std::set< std::string > & fileSet, NifModel * nif, const std::string & filePath )
{
	if ( filePath.empty() )
		return;
	if ( nif->getBSVersion() < 170 || !( filePath.ends_with( ".mat" ) && filePath.starts_with( "materials/" ) ) ) {
		if ( nif->findResourceFile( QString::fromStdString( filePath ), nullptr, nullptr ).isEmpty() )
			return;
	}
	fileSet.insert( filePath );
}

void spExtractAllResources::findPaths( std::set< std::string > & fileSet, NifModel * nif, const NifItem * item )
{
	if ( spResourceFileExtract::is_Applicable( nif, item ) ) {
		std::string	filePath( spResourceFileExtract::getNifItemFilePath( nif, item ) );
		addPath( fileSet, nif, filePath );
		if ( Game::GameManager::get_game( nif ) == Game::OBLIVION && filePath.ends_with( ".dds" ) ) {
			filePath.resize( filePath.length() - 4 );
			filePath += "_n.dds";
			addPath( fileSet, nif, filePath );
			filePath[filePath.length() - 5] = 'g';
			addPath( fileSet, nif, filePath );
		}
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
		QMessageBox::critical( nullptr, "NifSkope error", QString( "Error extracting file: %1" ).arg( e.what() ) );
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
	bool constant() const override final { return true; }

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
			} catch ( NifSkopeError & e ) {
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
		QMessageBox::critical( nullptr, "NifSkope error", QString( "Error extracting file: %1" ).arg( e.what() ) );
	}
	return index;
}

REGISTER_SPELL( spExtractAllMaterials )

//! Convert Starfield BSGeometry block(s) to use external geometry data
class spMeshFileExport final : public Spell
{
public:
	static constexpr const char * kMissingOutputDirectoryError =
		"missing output directory for external mesh export; use -o <folder> (CLI) or set it in GUI via Convert to External Geometry first";

	QString name() const override final { return Spell::tr( "Convert to External Geometry" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( !( nif && nif->getBSVersion() >= 170 ) )
			return false;
		const NifItem *	item = nif->getItem( index, false );
		if ( !item )
			return true;
		return ( item->hasName( "BSGeometry" ) && ( nif->get<quint32>(item, "Flags") & 0x0200 ) != 0 );
	}

	static void saveMeshData( QByteArray & meshBuf, NifModel * nif, const NifItem * meshDataItem );
	static bool processItem( NifModel * nif, NifItem * item, const std::string & outputDirectory, const QString & meshDir );
	static bool processAllItems( NifModel * nif );
	static bool processAllItems( NifModel * nif, const QString & outputDirectory );
	static bool processAllItems( NifModel * nif, const std::string & outputDirectory, const QString & meshDir );
	static QString getNormalizedMeshDir();
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

QString spMeshFileExport::getNormalizedMeshDir()
{
	QSettings	settings;
	QString	meshDir = settings.value( "Settings/Importex/Mesh Export Dir", QString() ).toString().trimmed().toLower();
	meshDir.replace( QChar('/'), QChar('\\') );
	while ( meshDir.endsWith( QChar('\\') ) )
		meshDir.chop( 1 );
	while ( meshDir.startsWith( QChar('\\') ) )
		meshDir.remove( 0, 1 );
	if ( !meshDir.isEmpty() )
		meshDir.append( QChar('\\') );
	return meshDir;
}

void spMeshFileExport::saveMeshData( QByteArray & meshBuf, NifModel * nif, const NifItem * meshDataItem )
{
	{
		QBuffer	tmpBuf( &meshBuf );
		tmpBuf.open( QIODevice::WriteOnly );
		NifOStream	nifStream( nif, &tmpBuf );
		nif->saveItem( meshDataItem, nifStream );
	}
	if ( !( nif->get<quint32>( meshDataItem, "Num Meshlets" ) | nif->get<quint32>( meshDataItem, "Num Cull Data" ) ) )
		meshBuf.chop( 8 );	// end of file after LODs if there are no meshlets
}

bool spMeshFileExport::processItem(
	NifModel * nif, NifItem * item, const std::string & outputDirectory, const QString & meshDir )
{
	quint32	flags;
	if ( !( item && item->hasName( "BSGeometry" ) && ( (flags = nif->get<quint32>(item, "Flags")) & 0x0200 ) != 0 ) )
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
			saveMeshData( meshBuf, nif, nif->getItem( meshData, false ) );

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
				QMessageBox::critical( nullptr, "NifSkope error",
										QString( "Error extracting file: %1" ).arg( e.what() ) );
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

bool spMeshFileExport::processAllItems( NifModel * nif, const std::string & outputDirectory, const QString & meshDir )
{
	bool r = false;
	for ( int b = 0; b < nif->getBlockCount(); b++ )
		r = r | processItem( nif, nif->getBlockItem( qint32(b) ), outputDirectory, meshDir );
	return r;
}

bool spMeshFileExport::processAllItems( NifModel * nif )
{
	if ( !( nif && nif->getBSVersion() >= 170 ) )
		return false;

	std::string	outputDirectory( spResourceFileExtract::getOutputDirectory( nif ) );
	if ( outputDirectory.empty() ) {
		if ( nif->getBatchProcessingMode() ) {
			throw NifSkopeError( kMissingOutputDirectoryError );
		}
		return false;
	}

	QString	meshDir = getNormalizedMeshDir();

	bool meshesConverted = processAllItems( nif, outputDirectory, meshDir );
	if ( meshesConverted && !nif->getBatchProcessingMode() )
		Game::GameManager::close_resources();
	return meshesConverted;
}

bool spMeshFileExport::processAllItems( NifModel * nif, const QString & outputDirectory )
{
	if ( !( nif && nif->getBSVersion() >= 170 ) )
		return false;

	QString outputDir = QDir::fromNativeSeparators( outputDirectory ).trimmed();
	while ( outputDir.endsWith( QChar('/') ) )
		outputDir.chop( 1 );
	if ( outputDir.isEmpty() ) {
		if ( nif->getBatchProcessingMode() )
			throw NifSkopeError( kMissingOutputDirectoryError );
		return false;
	}

	std::string outputDirectoryStd( outputDir.toLocal8Bit().constData() );
	if ( !outputDirectoryStd.empty() ) {
		char c = outputDirectoryStd.back();
		if ( c != '/' && c != '\\' )
			outputDirectoryStd += '/';
	}

	QString	meshDir = getNormalizedMeshDir();

	bool meshesConverted = processAllItems( nif, outputDirectoryStd, meshDir );
	if ( meshesConverted && !nif->getBatchProcessingMode() )
		Game::GameManager::close_resources();
	return meshesConverted;
}

QModelIndex spMeshFileExport::cast( NifModel * nif, const QModelIndex & index )
{
	if ( !( nif && nif->getBSVersion() >= 170 ) )
		return index;

	NifItem *	item = nif->getItem( index, false );
	if ( item && !( item->hasName( "BSGeometry" ) && (nif->get<quint32>(item, "Flags") & 0x0200) != 0 ) )
		return index;

	bool	meshesConverted = false;
	if ( item ) {
		std::string	outputDirectory( spResourceFileExtract::getOutputDirectory( nif ) );
		if ( outputDirectory.empty() )
			return index;

		QString	meshDir = getNormalizedMeshDir();

		meshesConverted = processItem( nif, item, outputDirectory, meshDir );
	} else {
		meshesConverted = processAllItems( nif );
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

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( !( nif && nif->getBSVersion() >= 170 ) )
			return false;
		const NifItem *	item = nif->getItem( index, false );
		if ( !item )
			return true;
		return ( item->hasName( "BSGeometry" ) && ( nif->get<quint32>(item, "Flags") & 0x0200 ) == 0 );
	}

	static bool processItem( NifModel * nif, NifItem * item );
	static bool processAllItems( NifModel * nif );
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

bool spMeshFileImport::processItem( NifModel * nif, NifItem * item )
{
	quint32	flags;
	if ( !( item && item->hasName( "BSGeometry" ) && ( (flags = nif->get<quint32>(item, "Flags")) & 0x0200 ) == 0 ) )
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
					throw NifSkopeError( "failed to load mesh file '%s'", meshPath.toStdString().c_str() );
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
	if ( item && !( item->hasName( "BSGeometry" ) && (nif->get<quint32>(item, "Flags") & 0x0200) == 0 ) )
		return index;

	if ( item )
		processItem( nif, item );
	else
		processAllItems( nif );

	return index;
}

REGISTER_SPELL( spMeshFileImport )

//! Save a single Starfield .mesh file to a user specified path
class spMeshFileSaveAs final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Save As" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }
	bool constant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		if ( !( nif && nif->getBSVersion() >= 170 ) )
			return false;
		const NifItem *	item = nif->getItem( index, false );
		if ( !item )
			return false;
		return ( item->hasName( "Mesh Data" ) && nif->blockInherits( item, "BSGeometry" ) );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		const NifItem *	item = nif->getItem( index, false );
		if ( !( item && item->hasName( "Mesh Data" ) ) )
			return index;

		QByteArray	meshBuf;
		spMeshFileExport::saveMeshData( meshBuf, nif, item );
		if ( meshBuf.isEmpty() )
			return index;

		QString	prvPath;
		{
			QSettings	settings;
			prvPath = settings.value( "Spells/Mesh/Save As/Last File Path", QString() ).toString();
		}
		QString	fileName = QFileDialog::getSaveFileName( qApp->activeWindow(),
														QString( "Choose a .mesh file for export" ),
														prvPath, QString( "Starfield mesh (*.mesh)" ) );
		if ( fileName.isEmpty() )
			return index;
		if ( !fileName.endsWith( QLatin1StringView( ".mesh" ), Qt::CaseInsensitive ) )
			fileName.append( ".mesh" );
		if ( fileName != prvPath ) {
			QSettings	settings;
			settings.setValue( "Spells/Mesh/Save As/Last File Path", QVariant( fileName ) );
		}

		QFile	meshFile( fileName );
		if ( meshFile.open( QIODevice::WriteOnly ) )
			meshFile.write( meshBuf );
		else
			QMessageBox::critical( nullptr, "NifSkope error", QString( "Could not open output file" ) );

		return index;
	}
};

REGISTER_SPELL( spMeshFileSaveAs )


//! Batch process multiple NIF files
class spBatchProcessFiles final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Process Multiple NIF Files" ); }
	QString page() const override final { return Spell::tr( "Batch" ); }
	bool constant() const override final { return true; }

	bool isApplicable( [[maybe_unused]] const NifModel * nif, const QModelIndex & index ) override final
	{
		return !index.isValid();
	}

	enum {
		spellFlagInternalGeom = 1,
		spellFlagRemoveUnusedStrings = 2,
		spellFlagRemoveDuplicateVerts = 4,
		spellFlagRemoveUnusedVerts = 8,
		spellFlagSimplify = 16,
		spellFlagLODGen = 32,
		spellFlagOptimizeIndices = 64,
		spellFlagTangentSpace = 128,
		spellFlagMeshlets = 256,
		spellFlagUpdateBounds = 512,
		spellFlagCombineProperties = 1024,
		spellFlagRemoveBogusNodes = 2048,
		spellFlagExternalGeom = 4096,
		spellFlagReorderBlocks = 8192,
		spellFlagSanitize = 16384
	};
	static bool processFile( NifModel * nif, void * p );
	static void findNIFFiles( QStringList & fileList, const QStringList & folderList );
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

#define DECLARE_SPELL_CAST_STATIC( sp )	\
	class sp	\
	{	\
	public:	\
		static QModelIndex cast_Static( NifModel * nif, const QModelIndex & index );	\
	};

DECLARE_SPELL_CAST_STATIC( spRemoveUnusedStrings )
DECLARE_SPELL_CAST_STATIC( spSimplifyAllBSTriShapes )
DECLARE_SPELL_CAST_STATIC( spSimplifySFMesh )
DECLARE_SPELL_CAST_STATIC( spRemoveAllDuplicateVertices )
DECLARE_SPELL_CAST_STATIC( spRemoveAllWasteVertices )
DECLARE_SPELL_CAST_STATIC( spOptimizeAllIndices )
DECLARE_SPELL_CAST_STATIC( spAddAllTangentSpaces )
DECLARE_SPELL_CAST_STATIC( spGenerateMeshlets )
DECLARE_SPELL_CAST_STATIC( spUpdateAllBounds )
DECLARE_SPELL_CAST_STATIC( spCombiProps )
DECLARE_SPELL_CAST_STATIC( spRemoveBogusNodes )
DECLARE_SPELL_CAST_STATIC( spSanitizeBlockOrder )

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

	if ( spellMask & spellFlagRemoveDuplicateVerts ) {
		spRemoveAllDuplicateVertices::cast_Static( nif, QModelIndex() );
		fileChanged = true;
	}

	if ( spellMask & spellFlagRemoveUnusedVerts ) {
		spRemoveAllWasteVertices::cast_Static( nif, QModelIndex() );
		fileChanged = true;
	}

	if ( ( spellMask & spellFlagSimplify ) && nif->getBSVersion() >= 100 && nif->getBSVersion() < 170 ) {
		spSimplifyAllBSTriShapes::cast_Static( nif, QModelIndex() );
		fileChanged = true;
	}

	if ( ( spellMask & spellFlagLODGen ) && nif->getBSVersion() >= 170 ) {
		spSimplifySFMesh::cast_Static( nif, QModelIndex() );
		fileChanged = true;
	}

	if ( spellMask & spellFlagOptimizeIndices ) {
		spOptimizeAllIndices::cast_Static( nif, QModelIndex() );
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

	if ( spellMask & spellFlagCombineProperties ) {
		spCombiProps::cast_Static( nif, QModelIndex() );
		fileChanged = true;
	}

	if ( spellMask & spellFlagRemoveBogusNodes ) {
		spRemoveBogusNodes::cast_Static( nif, QModelIndex() );
		fileChanged = true;
	}

	if ( ( spellMask & spellFlagExternalGeom ) && nif->getBSVersion() >= 170 ) {
		spMeshFileExport	sp;
		sp.cast( nif, QModelIndex() );
		fileChanged = true;
	}

	if ( spellMask & spellFlagReorderBlocks ) {
		spSanitizeBlockOrder::cast_Static( nif, QModelIndex() );
		fileChanged = true;
	}

	if ( spellMask & spellFlagSanitize ) {
		SpellBook::sanitize( nif );
		fileChanged = true;
	}

	return fileChanged;
}

void spBatchProcessFiles::findNIFFiles( QStringList & fileList, const QStringList & folderList )
{
	for ( const QString & dirName : folderList ) {
		QDir	d( dirName );
		if ( !d.exists() )
			continue;
		QStringList	e = d.entryList( QDir::AllDirs | QDir::Readable | QDir::NoDotAndDotDot,
										QDir::Name | QDir::IgnoreCase );
		for ( QString & f : e )
			f = d.filePath( f );
		findNIFFiles( fileList, e );
		d.setNameFilters( { "*.nif" } );
		e = d.entryList( QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase );
		for ( const QString & f : e )
			fileList.append( d.filePath( f ) );
	}
}

QModelIndex spBatchProcessFiles::cast( [[maybe_unused]] NifModel * nif, const QModelIndex & index )
{
	if ( index.isValid() )
		return index;

	int	spellMask = 0;
	bool	folderMode;
	{
		QDialog	dlg;
		QLabel *	lb = new QLabel( &dlg );
		lb->setText( "Batch process multiple models, overwriting the original NIF files" );
		QLabel *	lb2 = new QLabel( "Select spells to be cast, in the order listed:", &dlg );
		QCheckBox *	checkInternalGeom = new QCheckBox( "Convert to Internal Geometry", &dlg );
		QCheckBox *	checkRemoveUnusedStrings = new QCheckBox( "Remove Unused Strings", &dlg );
		QCheckBox *	checkRemoveDuplicateVertices = new QCheckBox( "Remove Duplicate Vertices", &dlg );
		QCheckBox *	checkRemoveUnusedVertices = new QCheckBox( "Remove Unused Vertices", &dlg );
		QCheckBox *	checkSimplify = new QCheckBox( "Simplify All BSTriShapes", &dlg );
		QCheckBox *	checkLODGen = new QCheckBox( "Generate Starfield LODs", &dlg );
		QCheckBox *	checkOptimizeIndices = new QCheckBox( "Optimize Indices", &dlg );
		QCheckBox *	checkTangentSpace = new QCheckBox( "Add Tangent Spaces and Update", &dlg );
		QCheckBox *	checkMeshlets = new QCheckBox( "Generate Meshlets and Update Bounds", &dlg );
		QCheckBox *	checkUpdateBounds = new QCheckBox( "Update Bounds", &dlg );
		QCheckBox *	checkCombineProperties = new QCheckBox( "Combine Properties", &dlg );
		QCheckBox *	checkRemoveBogusNodes = new QCheckBox( "Remove Bogus Nodes", &dlg );
		QCheckBox *	checkExternalGeom = new QCheckBox( "Convert to External Geometry", &dlg );
		QCheckBox *	checkReorderBlocks = new QCheckBox( "Reorder Blocks", &dlg );
		QCheckBox *	checkSanitize = new QCheckBox( "Sanitize before Save", &dlg );
		QCheckBox *	checkSelectFolder = new QCheckBox( "Select and Process Folder", &dlg );
		QPushButton *	okButton = new QPushButton( "OK", &dlg );
		QPushButton *	cancelButton = new QPushButton( "Cancel", &dlg );

		QGridLayout *	grid = new QGridLayout;
		dlg.setLayout( grid );
		grid->addWidget( lb, 0, 0, 1, 5 );
		grid->addWidget( new QLabel( "", &dlg ), 1, 0, 1, 5 );
		grid->addWidget( lb2, 2, 0, 1, 5 );
		grid->addWidget( checkInternalGeom, 3, 0, 1, 5 );
		grid->addWidget( checkRemoveUnusedStrings, 4, 0, 1, 5 );
		grid->addWidget( checkRemoveDuplicateVertices, 5, 0, 1, 5 );
		grid->addWidget( checkRemoveUnusedVertices, 6, 0, 1, 5 );
		grid->addWidget( checkSimplify, 7, 0, 1, 5 );
		grid->addWidget( checkLODGen, 8, 0, 1, 5 );
		grid->addWidget( checkOptimizeIndices, 9, 0, 1, 5 );
		grid->addWidget( checkTangentSpace, 10, 0, 1, 5 );
		grid->addWidget( checkMeshlets, 11, 0, 1, 5 );
		grid->addWidget( checkUpdateBounds, 12, 0, 1, 5 );
		grid->addWidget( checkCombineProperties, 13, 0, 1, 5 );
		grid->addWidget( checkRemoveBogusNodes, 14, 0, 1, 5 );
		grid->addWidget( checkExternalGeom, 15, 0, 1, 5 );
		grid->addWidget( checkReorderBlocks, 16, 0, 1, 5 );
		grid->addWidget( checkSanitize, 17, 0, 1, 5 );
		grid->addWidget( new QLabel( "", &dlg ), 18, 0, 1, 5 );
		grid->addWidget( checkSelectFolder, 19, 0, 1, 5 );
		grid->addWidget( new QLabel( "", &dlg ), 20, 0, 1, 5 );
		grid->addWidget( okButton, 21, 1, 1, 1 );
		grid->addWidget( cancelButton, 21, 3, 1, 1 );

		QObject::connect( okButton, &QPushButton::clicked, &dlg, &QDialog::accept );
		QObject::connect( cancelButton, &QPushButton::clicked, &dlg, &QDialog::reject );

		if ( dlg.exec() != QDialog::Accepted )
			return index;

		if ( checkInternalGeom->isChecked() )
			spellMask = spellFlagInternalGeom;
		if ( checkRemoveUnusedStrings->isChecked() )
			spellMask = spellMask | spellFlagRemoveUnusedStrings;
		if ( checkRemoveDuplicateVertices->isChecked() )
			spellMask = spellMask | spellFlagRemoveDuplicateVerts;
		if ( checkRemoveUnusedVertices->isChecked() )
			spellMask = spellMask | spellFlagRemoveUnusedVerts;
		if ( checkSimplify->isChecked() )
			spellMask = spellMask | spellFlagSimplify;
		if ( checkLODGen->isChecked() )
			spellMask = spellMask | spellFlagLODGen;
		if ( checkOptimizeIndices->isChecked() )
			spellMask = spellMask | spellFlagOptimizeIndices;
		if ( checkTangentSpace->isChecked() )
			spellMask = spellMask | spellFlagTangentSpace;
		if ( checkMeshlets->isChecked() )
			spellMask = spellMask | spellFlagMeshlets;
		if ( checkUpdateBounds->isChecked() )
			spellMask = spellMask | spellFlagUpdateBounds;
		if ( checkCombineProperties->isChecked() )
			spellMask = spellMask | spellFlagCombineProperties;
		if ( checkRemoveBogusNodes->isChecked() )
			spellMask = spellMask | spellFlagRemoveBogusNodes;
		if ( checkExternalGeom->isChecked() )
			spellMask = spellMask | spellFlagExternalGeom;
		if ( checkReorderBlocks->isChecked() )
			spellMask = spellMask | spellFlagReorderBlocks;
		if ( checkSanitize->isChecked() )
			spellMask = spellMask | spellFlagSanitize;
		if ( !spellMask )
			return index;

		folderMode = checkSelectFolder->isChecked();
	}

	QStringList	fileList;
	if ( folderMode ) {
		QFileDialog	fd( nullptr, "Select Folder to Process" );
		fd.setFileMode( QFileDialog::Directory );
		fd.setOptions( QFileDialog::ShowDirsOnly );
		if ( fd.exec() )
			findNIFFiles( fileList, fd.selectedFiles() );
	} else {
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

