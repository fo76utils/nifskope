#include "spellbook.h"

#include <QDialog>
#include <QFileDialog>
#include <QSettings>

#include "gamemanager.h"
#include "libfo76utils/src/common.hpp"
#include "libfo76utils/src/filebuf.hpp"
#include "libfo76utils/src/material.hpp"

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

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		const NifItem * item = nif->getItem( index );
		if ( !item )
			return false;
		NifValue::Type	vt = item->valueType();
		if ( vt != NifValue::tStringIndex && vt != NifValue::tSizedString ) {
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
			if ( item->name() == "Path" || item->name() == "Mesh Path" )
				break;
			return false;
		} while ( false );
		return !( nif->resolveString( item ).isEmpty() );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

static void writeFileWithPath( const char * fileName, const void * buf, size_t bufSize )
{
	OutputFile *	f = nullptr;
	try {
		f = new OutputFile( fileName, 0 );
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
		f = new OutputFile( fileName, 0 );
	}

	try {
		f->writeData( buf, bufSize );
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
	Game::GameMode	game = Game::GameManager::get_game( nif->getVersionNumber(), nif->getUserVersion(), nif->getBSVersion() );

	const NifItem * item = nif->getItem( index );
	if ( !item )
		return index;

	const char *	archiveFolder = nullptr;
	const char *	extension = nullptr;

	if ( item->parent() && nif->getBSVersion() >= 130 && item->name() == "Name" ) {
		if ( item->parent()->name() == "BSLightingShaderProperty" ) {
			archiveFolder = "materials/";
			extension = ( nif->getBSVersion() < 160 ? ".bgsm" : ".mat" );
		} else if ( item->parent()->name() == "BSEffectShaderProperty" ) {
			archiveFolder = "materials/";
			extension = ( nif->getBSVersion() < 160 ? ".bgem" : ".mat" );
		}
	} else if ( ( item->parent() && item->parent()->name() == "Textures" ) || item->name().contains( "Texture" ) || ( nif->getBSVersion() >= 160 && item->name() == "Path" ) ) {
		archiveFolder = "textures/";
		extension = ".dds";
	} else if ( nif->getBSVersion() >= 160 && item->name() == "Mesh Path" ) {
		archiveFolder = "geometries/";
		extension = ".mesh";
	}

	QString	filePath( nif->resolveString( item ) );
	if ( filePath.isEmpty() )
		return index;
	std::string	matFileData;
	try {
		if ( extension && std::string( extension ) == ".mat" ) {
			std::string	matFilePath( Game::GameManager::get_full_path( filePath, archiveFolder, extension ) );
			filePath = QString::fromStdString( matFilePath );
			CE2MaterialDB *	materials = Game::GameManager::materials( game );
			if ( materials ) {
				(void) materials->loadMaterial( matFilePath );
				materials->getJSONMaterial( matFileData, matFilePath );
			}
			if ( matFileData.empty() )
				return index;
		} else {
			filePath = Game::GameManager::find_file( game, filePath, archiveFolder, extension );
			if ( filePath.isEmpty() )
				return index;
		}

		QSettings	settings;
		QString	key = QString( "%1/%2/%3/Last File Path" ).arg( "Spells", page(), name() );
		QString	dstPath( settings.value( key ).toString() );
		{
			QFileDialog	dialog;
			dialog.setFileMode( QFileDialog::Directory );
			if ( !dstPath.isEmpty() )
				dialog.selectFile( dstPath );
			if ( !dialog.exec() )
				return index;
			dstPath = dialog.selectedFiles().at( 0 );
		}
		if ( dstPath.isEmpty() )
			return index;
		settings.setValue( key, QVariant(dstPath) );

		std::string	fullPath( dstPath.replace( QChar('\\'), QChar('/') ).toStdString() );
		if ( !fullPath.ends_with( '/' ) )
			fullPath += '/';
		fullPath += filePath.toStdString();

		if ( !matFileData.empty() ) {
			matFileData += '\n';
			writeFileWithPath( fullPath.c_str(), matFileData.c_str(), matFileData.length() );
		} else {
			std::vector< unsigned char >	fileData;
			if ( Game::GameManager::get_file( fileData, game, filePath, nullptr, nullptr ) )
				writeFileWithPath( fullPath.c_str(), fileData.data(), fileData.size() );
		}
	} catch ( std::exception & e ) {
		QMessageBox::critical( nullptr, "NifSkope error", QString("Error extracting file: %1" ).arg( e.what() ) );
	}
	return index;
}

REGISTER_SPELL( spResourceFileExtract )

