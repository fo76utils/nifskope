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

#ifndef GLTEX_H
#define GLTEX_H

#include <QObject> // Inherited
#include <QByteArray>
#include <QHash>
#include <QPersistentModelIndex>
#include <QString>
#include <QStringView>


//! @file gltex.h TexCache etc. header

class NifModel;
class QOpenGLContext;
class QSettings;

typedef unsigned int GLuint;
typedef unsigned int GLenum;

/*! A class for handling OpenGL textures.
 *
 * This class stores information on all loaded textures.
 */
class TexCache final : public QObject
{
	Q_OBJECT

public:
	struct TexFmt
	{
		enum {
			// for imageFormat
			TEXFMT_UNKNOWN = 0,
			TEXFMT_BMP = 1,
			TEXFMT_DDS = 2,
			TEXFMT_NIF = 3,
			TEXFMT_TGA = 4,
			// flags for imageEncoding
			TEXFMT_DXT1 = 8,
			TEXFMT_DXT3 = 16,
			TEXFMT_DXT5 = 32,
			TEXFMT_GRAYSCALE = 256,
			TEXFMT_GRAYSCALE_ALPHA = 512,
			TEXFMT_PAL8 = 1024,
			TEXFMT_RGB8 = 2048,
			TEXFMT_RGBA8 = 4096,
			TEXFMT_RLE = 8192
		};
		GLuint internalFormat = 0;	// OpenGL internal format
		bool isCompressed = false;
		unsigned char imageFormat = 0;
		unsigned short imageEncoding = 0;
		QString toString() const;
	};

	//! A structure for storing information on a single texture.
	struct Tex
	{
		//! The texture file name.
		QString filename;
		//! The texture file path.
		QString filepath;
		//! IDs for use with GL texture functions
		GLuint id[2];
		//! The format target
		GLenum target = 0; // = 0x0DE1; // GL_TEXTURE_2D
		//! Width of the texture
		GLuint width = 0;
		//! Height of the texture
		GLuint height = 0;
		//! Number of mipmaps present
		GLuint mipmaps = 0;
		//! Format of the texture
		TexFmt format;
		//! Status messages
		QString status;

		//! Load the texture
		void load( const NifModel * nif );

		//! Save the texture as a file
		bool saveAsFile( const QModelIndex & index, QString & savepath );
		//! Save the texture as pixel data
		bool savePixelData( NifModel * nif, const QModelIndex & iSource, QModelIndex & iData ) const;
		//! Returns true if load() was called and at least one valid texture ID was generated
		inline bool isLoaded() const
		{
			return bool( ( id[0] + 1U ) & ~1U );
		}
	};

	TexCache( QObject * parent = nullptr );
	~TexCache();

	//! Bind a texture from filename
	int bind( const QStringView & fname, const NifModel * nif = nullptr, bool useSecondTexture = false );
	//! Bind a texture from pixel data
	int bind( const QModelIndex & iSource );

	//! Debug function for getting info about a texture
	QString info( const QModelIndex & iSource );

	//! Export pixel data to a file
	bool exportFile( const QModelIndex & iSource, QString & filepath );
	//! Import pixel data from a file (not implemented yet)
	bool importFile( NifModel * nif, const QModelIndex & iSource, QModelIndex & iData );

	//! Find a texture based on its filename
	static QString find( const QString & file, const NifModel * nif = nullptr );
	//! Remove the path from a filename
	static QString stripPath( const QString & file, const QString & nifFolder );
	//! Checks whether the given file can be loaded
	static bool canLoad( const QString & file );
	//! Checks whether the extension is supported
	static bool isSupported( const QString & file );

	//! Number of texture units
	enum	{ maxTextureUnits = 32 };
	static int	num_texture_units;	// for glActiveTexture()
	static int	num_txtunits_client;	// for glClientActiveTexture()
	static int	pbrCubeMapResolution;
	static int	pbrImportanceSamples;
	static int	hdrToneMapLevel;

signals:
	void sigRefresh();

public slots:
	void flush();

	/*! Set the folder to read textures from
	 *
	 * If this is not set, relative paths won't resolve. The standard usage
	 * is to give NifModel::getFolder() as the argument.
	 */
	void setNifFolder( const QString & );

protected:
	Tex ** textures;
	std::uint32_t textureHashMask;
	std::uint32_t textureCount;
	QHash<QModelIndex, Tex *> embedTextures;

	inline Tex * insertTex( const QStringView & file );
	void rehashTextures();

public:
	const Tex * getTextureInfo( const QStringView & file ) const;

	// returns true if the settings have changed
	static bool loadSettings( QSettings & settings );
	static void clearCubeCache();
};

void initializeTextureUnits( const QOpenGLContext * );

bool activateTextureUnit( int x, bool noClient = false );
void resetTextureUnits( int numTex = TexCache::maxTextureUnits );

float get_max_anisotropy();

#endif
