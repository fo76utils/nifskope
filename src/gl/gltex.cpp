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
int TexCache::pbrCubeMapResolution = 512;
int TexCache::pbrImportanceSamples = 256;
int TexCache::hdrToneMapLevel = 8;

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
		tmp = std::min( std::max( tmp, GLint(1) ), GLint(TexCache::maxTextureUnits) );
		TexCache::num_texture_units = tmp;
		glGetIntegerv( GL_MAX_TEXTURE_COORDS, &tmp );
		TexCache::num_txtunits_client = std::max( tmp, GLint(1) );

		//qDebug() << "texture units" << TexCache::num_texture_units;
	} else {
		qCWarning( nsGl ) << QObject::tr( "Multitexturing not supported." );
		TexCache::num_texture_units = 0;
		TexCache::num_txtunits_client = 0;
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
	if ( stage < TexCache::num_texture_units ) [[likely]] {

		glActiveTexture( GL_TEXTURE0 + stage );
		if ( stage < TexCache::num_txtunits_client && !noClient )
			glClientActiveTexture( GL_TEXTURE0 + stage );
		return true;
	}

	return ( stage == 0 );
}

void resetTextureUnits( int numTex )
{
	if ( !TexCache::num_texture_units ) {
		glDisable( GL_TEXTURE_2D );
		return;
	}

	for ( int x = std::min( numTex, TexCache::num_texture_units ); --x >= 0; ) {
		glActiveTexture( GL_TEXTURE0 + x );
		glDisable( GL_TEXTURE_2D );
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
	}
	for ( int x = TexCache::num_txtunits_client; --x >= 0; ) {
		glClientActiveTexture( GL_TEXTURE0 + x );
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	}
}


/*
 *  TexCache
 */

QString TexCache::TexFmt::toString() const
{
	if ( imageFormat != TEXFMT_UNKNOWN ) {
		QString	format;
		switch ( imageFormat ) {
		case TEXFMT_BMP:
			format = "BMP";
			break;
		case TEXFMT_DDS:
			format = "DDS";
			break;
		case TEXFMT_NIF:
			format = "NIF";
			break;
		default:
			format = "TGA";
			break;
		}
		if ( imageEncoding & TEXFMT_DXT1 )
			format.append( " (DXT1)" );
		if ( imageEncoding & TEXFMT_DXT3 )
			format.append( " (DXT3)" );
		if ( imageEncoding & TEXFMT_DXT5 )
			format.append( " (DXT5)" );
		if ( imageEncoding & TEXFMT_GRAYSCALE )
			format.append( " (greyscale)" );
		if ( imageEncoding & TEXFMT_GRAYSCALE_ALPHA )
			format.append( " (greyscale) (alpha)" );
		if ( imageEncoding & TEXFMT_PAL8 )
			format.append( " (PAL8)" );
		if ( imageEncoding & TEXFMT_RGB8 )
			format.append( " (RGB8)" );
		if ( imageEncoding & TEXFMT_RGBA8 )
			format.append( " (RGBA8)" );
		if ( imageEncoding & TEXFMT_RLE )
			format.append( " (RLE)" );
		return format;
	}
	if ( imageEncoding & TEXFMT_DXT1 )
		return "(DXT1)";
	if ( imageEncoding & TEXFMT_DXT3 )
		return "(DXT3)";
	if ( imageEncoding & TEXFMT_DXT5 )
		return "(DXT5)";
	return QString();
}

TexCache::TexCache( QObject * parent ) : QObject( parent )
{
	textures = nullptr;
	textureHashMask = 0;
	textureCount = 0;
	rehashTextures();
}

TexCache::~TexCache()
{
#if 0
	flush();
#endif
	delete[] textures;
}

QString TexCache::find( const QString & file, const NifModel * nif )
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
		QString	fullPath;
		if ( !nif )
			fullPath = Game::GameManager::find_file( Game::OTHER, filename, "textures", extensions[i] );
		else
			fullPath = nif->findResourceFile( filename, "textures", extensions[i] );
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

const TexCache::Tex::ImageInfo * TexCache::getTextureInfo( const QStringView & file ) const
{
	if ( file.isEmpty() ) [[unlikely]]
		return nullptr;
	const QChar *	s = file.data();
	size_t	nameLen = size_t( file.size() );
	std::uint32_t	h = hashFunctionUInt32( s, nameLen * sizeof( QChar ) );
	std::uint32_t	m = textureHashMask;
	for ( h = h & m; textures[h].nameLen; h = ( h + 1U ) & m ) {
		const Tex &	p = textures[h];
		if ( p.nameLen == nameLen && std::memcmp( p.nameData, s, nameLen * sizeof( QChar ) ) == 0 )
			return p.imageInfo;
	}
	return nullptr;
}

static inline const QString & convertToQString( const QString & s )
{
	return s;
}

static inline QString convertToQString( const QStringView & s )
{
	return s.toString();
}

template< typename T > inline TexCache::Tex * TexCache::insertTex( const T & file )
{
	if ( file.isEmpty() ) [[unlikely]]
		return nullptr;
	const QChar *	s = file.data();
	size_t	nameLen = size_t( file.size() );
	std::uint32_t	h = hashFunctionUInt32( s, nameLen * sizeof( QChar ) );
	std::uint32_t	m = textureHashMask;
	for ( h = h & m; textures[h].nameLen; h = ( h + 1U ) & m ) {
		Tex &	p = textures[h];
		if ( p.nameLen == nameLen && std::memcmp( p.nameData, s, nameLen * sizeof( QChar ) ) == 0 )
			return &p;
	}

	if ( std::uint16_t( nameLen ) != nameLen ) [[unlikely]]
		return nullptr;
	Tex &	tx = textures[h];
	tx.imageInfo = new Tex::ImageInfo;
	tx.imageInfo->filename = convertToQString( file );
	tx.nameData = tx.imageInfo->filename.constData();
	tx.nameLen = std::uint16_t( nameLen );

	textureCount++;
	if ( ( std::uint64_t(textureCount) * 3U ) > ( std::uint64_t(textureHashMask) * 2U ) )
		return rehashTextures( &tx );

	return &tx;
}

TexCache::Tex * TexCache::rehashTextures( Tex * p )
{
	std::uint32_t	prvMask = textureHashMask;
	std::uint32_t	m = ( prvMask << 1 ) | 15U;
	size_t	n = size_t( m ) + 1;
	Tex *	newTextures = new Tex[n];
	Tex *	q = nullptr;
	if ( textureCount ) {
		for ( size_t i = 0; i <= prvMask; i++ ) {
			Tex &	tx = textures[i];
			if ( tx.nameLen ) {
				std::uint32_t	h = hashFunctionUInt32( tx.nameData, size_t( tx.nameLen ) * sizeof( QChar ) ) & m;
				while ( newTextures[h].nameLen )
					h = ( h + 1U ) & m;
				newTextures[h] = tx;
				if ( p == &tx ) [[unlikely]]
					q = newTextures + h;
			}
		}
	}
	delete[] textures;
	textures = newTextures;
	textureHashMask = m;
	return q;
}

int TexCache::bind( const QStringView & fname, const NifModel * nif )
{
	Tex *	tx = insertTex( fname );
	if ( !tx ) [[unlikely]]
		return 0;
	if ( !tx->isLoaded() ) [[unlikely]] {
		if ( tx->id[0] )
			return 0;

		return loadTex( *tx, nif );
	}

	if ( !tx->target ) [[unlikely]]
		tx->target = GL_TEXTURE_2D;
	glBindTexture( tx->target, tx->id[0] );

	return tx->mipmaps;
}

bool TexCache::bindCube( const QString & fname, const NifModel * nif, bool useSecondTexture )
{
	Tex *	tx = insertTex( fname );
	if ( !tx ) [[unlikely]]
		return false;

	if ( !tx->isLoaded() ) [[unlikely]] {
		if ( tx->id[0] || !loadTex( *tx, nif ) )
			return false;
	} else {
		if ( !tx->id[size_t(useSecondTexture)] ) [[unlikely]]
			return false;

		if ( !tx->target ) [[unlikely]]
			tx->target = GL_TEXTURE_CUBE_MAP;
	}

	glBindTexture( tx->target, tx->id[size_t(useSecondTexture)] );

	if ( !tx->mipmaps ) [[unlikely]]
		return false;

	glEnable( GL_TEXTURE_CUBE_MAP_SEAMLESS );

	glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
	glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	glMatrixMode( GL_TEXTURE );
	glLoadIdentity();
	glMatrixMode( GL_MODELVIEW );

	return true;
}

std::uint16_t TexCache::loadTex( Tex & tx, const NifModel * nif )
{
	Tex::ImageInfo *	i = tx.imageInfo;

	if ( !isSupported( i->filename ) ) {
		tx.id[0] = GLuint( -1 );
		return 0;
	}

	i->filepath = find( i->filename, nif );

	if ( !tx.id[0] )
		glGenTextures( 1, tx.id );

	if ( tx.target )
		glBindTexture( tx.target, tx.id[0] );

	try
	{
		i->mipmaps = texLoad( nif, i->filepath, i->format, tx.target, i->width, i->height, tx.id );
		tx.mipmaps = std::uint16_t( i->mipmaps );
	}
	catch ( QString & e )
	{
		i->status = e;
	}

	return tx.mipmaps;
}

int TexCache::bind( const QModelIndex & iSource )
{
	auto nif = NifModel::fromValidIndex(iSource);
	if ( nif ) {
		if ( nif->get<quint8>( iSource, "Use External" ) == 0 ) {
			QModelIndex iData = nif->getBlockIndex( nif->getLink( iSource, "Pixel Data" ) );

			if ( iData.isValid() ) {
				Tex &	tx = embedTextures[iData];
				Tex::ImageInfo *	i = tx.imageInfo;

				if ( !i ) {
					i = new Tex::ImageInfo;
					tx.imageInfo = i;
					try
					{
						glGenTextures( 1, tx.id );
						glBindTexture( GL_TEXTURE_2D, tx.id[0] );
						i->mipmaps = texLoad( iData, i->format, tx.target, i->width, i->height, tx.id );
						tx.mipmaps = std::uint16_t( i->mipmaps );
					}
					catch ( QString & e ) {
						i->status = e;
					}
				} else {
					glBindTexture( GL_TEXTURE_2D, tx.id[0] );
				}

				return tx.mipmaps;
			}
		} else if ( !nif->get<QString>( iSource, "File Name" ).isEmpty() ) {
			return bind( nif->get<QString>( iSource, "File Name" ), nif );
		}
	}

	return 0;
}

void TexCache::flush()
{
	for ( size_t i = 0; i <= textureHashMask; i++ ) {
		Tex &	tx = textures[i];
		if ( tx.isLoaded() )
			glDeleteTextures( ( !tx.id[1] ? 1 : 2 ), tx.id );
		if ( tx.imageInfo )
			delete tx.imageInfo;
	}
	textureHashMask = 0;
	textureCount = 0;
	rehashTextures();

	for ( Tex & tx : embedTextures ) {
		if ( tx.id[0] )
			glDeleteTextures( ( !tx.id[1] ? 1 : 2 ), tx.id );
		if ( tx.imageInfo )
			delete tx.imageInfo;
	}
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

			const Tex::ImageInfo *	i = nullptr;
			if ( iData.isValid() )
				i = embedTextures.value( iData ).imageInfo;
			if ( i ) {
				temp = QString( "Embedded texture: %1\nWidth: %2\nHeight: %3\nMipmaps: %4" )
						.arg( i->format.toString() )
						.arg( i->width )
						.arg( i->height )
						.arg( i->mipmaps );
			} else {
				temp = QString( "Embedded texture invalid" );
			}
		} else {
			QString filename = nif->get<QString>( iSource, "File Name" );
			const Tex::ImageInfo *	i = getTextureInfo( filename );
			if ( i ) {
				temp = QString( "External texture file: %1\nTexture path: %2\nFormat: %3\nWidth: %4\nHeight: %5\nMipmaps: %6" )
						.arg( i->filename )
						.arg( i->filepath )
						.arg( i->format.toString() )
						.arg( i->width )
						.arg( i->height )
						.arg( i->mipmaps );
			} else {
				temp = QString( "External texture file '%1' not found" ).arg( filename );
			}
		}
	}

	return temp;
}

bool TexCache::exportFile( const QModelIndex & iSource, QString & filepath )
{
	Tex &	tx = embedTextures[iSource];

	if ( !tx.imageInfo )
		tx.imageInfo = new Tex::ImageInfo;

	return tx.saveAsFile( iSource, filepath );
}

bool TexCache::importFile( NifModel * nif, const QModelIndex & iSource, QModelIndex & iData )
{
	// auto nif = NifModel::fromIndex(iSource);
	if ( nif && iSource.isValid() ) {
		if ( nif->get<quint8>( iSource, "Use External" ) == 1 ) {
			QString filename = nif->get<QString>( iSource, "File Name" );
			//qDebug() << "TexCache::importFile: Texture has filename (from NIF) " << filename;
			const Tex::ImageInfo *	i = getTextureInfo( filename );
			if ( i )
				return i->savePixelData( nif, iData );
		}
	}

	return false;
}


/*
*  TexCache::Tex
*/

bool TexCache::Tex::saveAsFile( const QModelIndex & index, QString & savepath )
{
	ImageInfo *	i = imageInfo;
	i->mipmaps = texLoad( index, i->format, target, i->width, i->height, id );
	mipmaps = std::uint16_t( i->mipmaps );

	if ( savepath.toLower().endsWith( ".tga" ) ) {
		return texSaveTGA( index, savepath, i->width, i->height );
	}

	return texSaveDDS( index, savepath, i->width, i->height, i->mipmaps );
}

bool TexCache::Tex::ImageInfo::savePixelData( NifModel * nif, QModelIndex & iData ) const
{
	// gltexloaders function goes here
	//qDebug() << "TexCache::Tex:savePixelData: Packing" << iSource << "from file" << filepath << "to" << iData;
	return texSaveNIF( nif, filepath, iData );
}

bool TexCache::loadSettings( QSettings & settings )
{
	int	tmp = settings.value( "Settings/Render/General/Ibl Cube Map Resolution", 2 ).toInt();
	tmp = 128 << std::min< int >( std::max< int >( tmp, 0 ), 4 );
	bool	r = ( tmp != pbrCubeMapResolution );
	pbrCubeMapResolution = tmp;

	tmp = settings.value( "Settings/Render/General/Ibl Importance Sample Cnt", 2 ).toInt();
	tmp = 64 << std::min< int >( std::max< int >( tmp, 0 ), 6 );
	r = r | ( tmp != pbrImportanceSamples );
	pbrImportanceSamples = tmp;

	tmp = settings.value( "Settings/Render/General/Hdr Tone Map", 8 ).toInt();
	tmp = std::min< int >( std::max< int >( tmp, 0 ), 16 );
	r = r | ( tmp != hdrToneMapLevel );
	hdrToneMapLevel = tmp;

	return r;
}

