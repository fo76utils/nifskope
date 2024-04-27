/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2015, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#include "gltex.h"

#include "message.h"
#include "gl/glscene.h"
#include "gl/gltexloaders.h"
#include "model/nifmodel.h"

#include "gamemanager.h"

#include <QDebug>
#include <QDir>
#include <QListView>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSettings>

#include <algorithm>


//! @file gltex.cpp TexCache management

#ifdef WIN32
static PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;
static PFNGLCLIENTACTIVETEXTUREPROC glClientActiveTexture = nullptr;
#endif

int TexCache::num_texture_units = 0;
int TexCache::num_txtunits_client = 0;
int TexCache::pbrCubeMapResolution = 256;

//! Maximum anisotropy
float max_anisotropy = 1.0f;
void set_max_anisotropy()
{
	static QSettings settings;
	int	tmp = roundFloat( settings.value( "Settings/Render/General/Anisotropic Filtering", 4.0 ).toFloat() );
	tmp = 1 << std::min< int >( std::max< int >( tmp, 0 ), 4 );
	max_anisotropy = std::min( float( tmp ), max_anisotropy );
}

float get_max_anisotropy()
{
	return max_anisotropy;
}

void initializeTextureUnits( const QOpenGLContext * context )
{
	if ( context->hasExtension( "GL_ARB_multitexture" ) ) {
		GLint	tmp = 0;
		glGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS, &tmp );
		tmp = std::min( std::max( tmp, GLint(1) ), GLint(32) );
		TexCache::num_texture_units = tmp;
		glGetIntegerv( GL_MAX_TEXTURE_COORDS, &tmp );
		TexCache::num_txtunits_client = std::max( tmp, GLint(1) );

		//qDebug() << "texture units" << TexCache::num_texture_units;
	} else {
		qCWarning( nsGl ) << QObject::tr( "Multitexturing not supported." );
		TexCache::num_texture_units = 1;
		TexCache::num_txtunits_client = 1;
	}

	if ( context->hasExtension( "GL_EXT_texture_filter_anisotropic" ) ) {
		glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_anisotropy );
		set_max_anisotropy();
		//qDebug() << "maximum anisotropy" << max_anisotropy;
	}

#ifdef WIN32
	if ( !glActiveTexture )
		glActiveTexture = (PFNGLACTIVETEXTUREPROC)context->getProcAddress( "glActiveTexture" );

	if ( !glClientActiveTexture )
		glClientActiveTexture = (PFNGLCLIENTACTIVETEXTUREPROC)context->getProcAddress( "glClientActiveTexture" );
#endif

	initializeTextureLoaders( context );
}

bool activateTextureUnit( int stage, bool noClient )
{
	if ( TexCache::num_texture_units <= 1 )
		return ( stage == 0 );

	if ( stage < TexCache::num_texture_units ) {

		glActiveTexture( GL_TEXTURE0 + stage );
		if ( stage < TexCache::num_txtunits_client && !noClient )
			glClientActiveTexture( GL_TEXTURE0 + stage );
		return true;
	}

	return false;
}

void resetTextureUnits( int numTex )
{
	if ( TexCache::num_texture_units <= 1 ) {
		glDisable( GL_TEXTURE_2D );
		return;
	}

	for ( int x = std::min( numTex, TexCache::num_texture_units ); --x >= 0; ) {
		glActiveTexture( GL_TEXTURE0 + x );
		glDisable( GL_TEXTURE_2D );
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
		if ( x < TexCache::num_txtunits_client ) {
			glClientActiveTexture( GL_TEXTURE0 + x );
			glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		}
	}
}


/*
 *  TexCache
 */

TexCache::TexCache( QObject * parent ) : QObject( parent )
{
}

TexCache::~TexCache()
{
	//flush();
}

QString TexCache::find( const QString & file, Game::GameMode game )
{
	if ( file.isEmpty() )
		return QString();
	if ( file.startsWith("#") && (file.length() == 9 || file.length() == 10) )
		return file;

	QString filename( file );

	static const char *	extensions[6] = {
		".dds", ".tga", ".png", ".bmp", ".nif", ".texcache"
	};

	// attempt to find the texture with one of the extensions
	for ( size_t i = 0; i < 6; i++ ) {
		QString	fullPath( Game::GameManager::find_file(game, filename, "textures", extensions[i]) );
		if ( !fullPath.isEmpty() )
			return fullPath;
		if ( i == 0 ) {
			QSettings settings;
			if ( !settings.value( "Settings/Resources/Alternate Extensions", false ).toBool() )
				break;
		}
	}

	return filename;
}

/*!
 * Note: all original morrowind nifs use name.ext only for addressing the
 * textures, but most mods use something like textures/[subdir/]name.ext.
 * This is due to a feature in Morrowind resource manager: it loads name.ext,
 * textures/name.ext and textures/subdir/name.ext but NOT subdir/name.ext.
 */
QString TexCache::stripPath( const QString & filepath, const QString & nifFolder )
{
	QString file = filepath;
	file = file.replace( "/", "\\" ).toLower();
	QDir basePath;

	QSettings settings;

	// TODO: New asset manager support
	QStringList folders = settings.value( "Settings/Resources/Folders", QStringList() ).toStringList();

	for ( QString base : folders ) {
		if ( base.startsWith( "./" ) || base.startsWith( ".\\" ) ) {
			base = nifFolder + "/" + base;
		}

		basePath.setPath( base );
		base = basePath.absolutePath();
		base = base.replace( "/", "\\" ).toLower();
		/*
		 * note that basePath.relativeFilePath( file ) here is *not*
		 * what we want - see the above doc comments for this function
		 */

		if ( file.startsWith( base ) ) {
			file.remove( 0, base.length() );
			break;
		}
	}

	if ( file.startsWith( "/" ) || file.startsWith( "\\" ) )
		file.remove( 0, 1 );

	return file;
}

bool TexCache::canLoad( const QString & filePath )
{
	return texCanLoad( filePath );
}

bool TexCache::isSupported( const QString & filePath )
{
	return texIsSupported( filePath );
}

int TexCache::bind( const QString & fname, Game::GameMode game, bool useSecondTexture )
{
	Tex * tx = textures.value( fname );
	if ( !tx ) [[unlikely]] {
		tx = new Tex;
		tx->filename = fname;
		tx->id[0] = 0;
		tx->id[1] = 0;
		tx->mipmaps = 0;
		tx->game = game;

		textures.insert( tx->filename, tx );

		if ( !isSupported( fname ) ) {
			tx->id[0] = 0xFFFFFFFF;
		} else {
			tx->filepath = find( tx->filename, game );
			tx->load();
			return tx->mipmaps;
		}
	}

	if ( tx->id[0] == 0xFFFFFFFF ) [[unlikely]]
		return 0;
	if ( !tx->id[size_t(useSecondTexture)] ) [[unlikely]]
		return 0;

	if ( !tx->target ) [[unlikely]]
		tx->target = GL_TEXTURE_2D;
	glBindTexture( tx->target, tx->id[size_t(useSecondTexture)] );

	return tx->mipmaps;
}

int TexCache::bind( const QModelIndex & iSource, Game::GameMode game )
{
	auto nif = NifModel::fromValidIndex(iSource);
	if ( nif ) {
		if ( nif->get<quint8>( iSource, "Use External" ) == 0 ) {
			QModelIndex iData = nif->getBlockIndex( nif->getLink( iSource, "Pixel Data" ) );

			if ( iData.isValid() ) {
				Tex * tx = embedTextures.value( iData );

				if ( !tx ) {
					tx = new Tex();
					tx->id[0] = 0;
					tx->id[1] = 0;
					tx->game = game;
					try
					{
						glGenTextures( 1, tx->id );
						glBindTexture( GL_TEXTURE_2D, tx->id[0] );
						embedTextures.insert( iData, tx );
						texLoad( iData, tx->format, tx->target, tx->width, tx->height, tx->mipmaps, tx->id );
					}
					catch ( QString & e ) {
						tx->status = e;
					}
				} else {
					glBindTexture( GL_TEXTURE_2D, tx->id[0] );
				}

				return tx->mipmaps;
			}
		} else if ( !nif->get<QString>( iSource, "File Name" ).isEmpty() ) {
			return bind( nif->get<QString>( iSource, "File Name" ), game );
		}
	}

	return 0;
}

void TexCache::flush()
{
	for ( Tex * tx : textures ) {
		if ( tx->id[0] )
			glDeleteTextures( ( !tx->id[1] ? 1 : 2 ), tx->id );
	}
	qDeleteAll( textures );
	textures.clear();

	for ( Tex * tx : embedTextures ) {
		if ( tx->id[0] )
			glDeleteTextures( ( !tx->id[1] ? 1 : 2 ), tx->id );
	}
	qDeleteAll( embedTextures );
	embedTextures.clear();
}

void TexCache::setNifFolder( const QString & folder )
{
	(void) folder;
	flush();
	emit sigRefresh();
}

QString TexCache::info( const QModelIndex & iSource )
{
	QString temp;

	auto nif = NifModel::fromValidIndex(iSource);
	if ( nif ) {
		if ( nif->get<quint8>( iSource, "Use External" ) == 0 ) {
			QModelIndex iData = nif->getBlockIndex( nif->getLink( iSource, "Pixel Data" ) );

			if ( iData.isValid() ) {
				Tex * tx = embedTextures.value( iData );
				temp = QString( "Embedded texture: %1\nWidth: %2\nHeight: %3\nMipmaps: %4" )
						.arg( tx->format )
						.arg( tx->width )
						.arg( tx->height )
						.arg( tx->mipmaps );
			} else {
				temp = QString( "Embedded texture invalid" );
			}
		} else {
			QString filename = nif->get<QString>( iSource, "File Name" );
			Tex * tx = textures.value( filename );
			temp = QString( "External texture file: %1\nTexture path: %2\nFormat: %3\nWidth: %4\nHeight: %5\nMipmaps: %6" )
					.arg( tx->filename )
					.arg( tx->filepath )
					.arg( tx->format )
					.arg( tx->width )
					.arg( tx->height )
					.arg( tx->mipmaps );
		}
	}

	return temp;
}

bool TexCache::exportFile( const QModelIndex & iSource, QString & filepath )
{
	Tex * tx = embedTextures.value( iSource );

	if ( !tx ) {
		tx = new Tex();
		tx->id[0] = 0;
		tx->id[1] = 0;
	}

	return tx->saveAsFile( iSource, filepath );
}

bool TexCache::importFile( NifModel * nif, const QModelIndex & iSource, QModelIndex & iData )
{
	// auto nif = NifModel::fromIndex(iSource);
	if ( nif && iSource.isValid() ) {
		if ( nif->get<quint8>( iSource, "Use External" ) == 1 ) {
			QString filename = nif->get<QString>( iSource, "File Name" );
			//qDebug() << "TexCache::importFile: Texture has filename (from NIF) " << filename;
			Tex * tx = textures.value( filename );
			return tx->savePixelData( nif, iSource, iData );
		}
	}

	return false;
}


/*
*  TexCache::Tex
*/

void TexCache::Tex::load()
{
	if ( !id[0] )
		glGenTextures( 1, id );

	width  = height = mipmaps = 0;
	status = QString();

	if ( target )
		glBindTexture( target, id[0] );

	try
	{
		QByteArray	data;
		texLoad( game, filepath, format, target, width, height, mipmaps, data, id );
	}
	catch ( QString & e )
	{
		status = e;
	}
}

bool TexCache::Tex::saveAsFile( const QModelIndex & index, QString & savepath )
{
	texLoad( index, format, target, width, height, mipmaps, id );

	if ( savepath.toLower().endsWith( ".tga" ) ) {
		return texSaveTGA( index, savepath, width, height );
	}

	return texSaveDDS( index, savepath, width, height, mipmaps );
}

bool TexCache::Tex::savePixelData( NifModel * nif, const QModelIndex & iSource, QModelIndex & iData )
{
	Q_UNUSED( iSource );
	// gltexloaders function goes here
	//qDebug() << "TexCache::Tex:savePixelData: Packing" << iSource << "from file" << filepath << "to" << iData;
	return texSaveNIF( game, nif, filepath, iData );
}
