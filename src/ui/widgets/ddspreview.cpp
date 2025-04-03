/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2025, NIF File Format Library and Tools
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

#include "ddspreview.h"
#include "sfcube2.hpp"
#include <thread>

#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSettings>

void DDSTexturePreview::threadFunction( DDSTexturePreview * p, std::uint32_t * imgBuf, int w, int h, int y0, int y1 )
{
	if ( y0 >= y1 || y0 >= h || y1 <= 0 )
		return;
	std::uint32_t *	d = imgBuf + ( size_t(y0) * size_t(w) );
	if ( !p->t ) {
		std::memset( d, 0, size_t(y1 - y0) * size_t(w) * sizeof(std::uint32_t) );
		return;
	}

	const DDSTexture16 &	t = *( p->t );
	float	m = p->mipLevel;
	if ( t.getIsCubeMap() ) {
		float	uScale = -6.28318531f / float( w );
		float	vScale = -3.14159265f / float( h );
		float	uOffset = uScale * 0.5f + 4.71238898f;
		float	vOffset = vScale * ( float(y0) + 0.5f ) + 1.57079633f;
		if ( p->textureFlags & 4 ) {
			vScale *= -1.0f;
			vOffset *= -1.0f;
		}
		FloatVector4	rx( float( std::cos(uScale) ), float( std::sin(uScale) ), 0.0f, 0.0f );
		FloatVector4	ry( float( std::cos(vScale) ), float( std::sin(vScale) ), 0.0f, 0.0f );
		FloatVector4	xc( float( std::cos(uOffset) ), float( std::sin(uOffset) ), 0.0f, 0.0f );
		FloatVector4	yc( float( std::cos(vOffset) ), float( std::sin(vOffset) ), 0.0f, 0.0f );
		rx.shuffleValues( 0x50 );
		rx[2] = rx[2] * -1.0f;
		ry.shuffleValues( 0x50 );
		ry[2] = ry[2] * -1.0f;
		float	scale = 1.0f;
		bool	isFloatFormat =		// R16G16B16A16_FLOAT, R9G9B9E5_SHAREDEXP or BC6H_UF16
			( t.getDXGIFormat() == 0x0A || t.getDXGIFormat() == 0x43 || t.getDXGIFormat() == 0x5F );
		if ( isFloatFormat ) {
			FloatVector4	c( 0.0f );
			for ( int i = 0; i < 6; i++ )
				c += FloatVector4::convertFloat16( t.getPixelN( 0, 0, 16, i ) );
			scale = std::max( c.dotProduct3( FloatVector4( 0.2126f, 0.7152f, 0.0722f, 1.0f ) ), 1.5f );
			scale = 1.5f / std::min( scale, 65536.0f );		// normalize float formats
		}
		for ( int y = y0; y < y1; y++ ) {
			for ( int x = 0; x < w; x++, d++ ) {
				FloatVector4	c( t.cubeMap( xc[0] * yc[0], xc[1] * yc[0], yc[1], m ) );
				if ( isFloatFormat ) {
					c *= scale;
					c = DDSTexture16::srgbCompress( c / ( c + 1.0f ) );
				}
				*d = std::uint32_t( c.shuffleValues( 0xC6 ) * 255.0f ) | 0xFF000000U;	// RGBA -> BGR
				xc = xc.shuffleValues( 0x14 ) * rx;
				xc = xc + FloatVector4( xc ).shuffleValues( 0xEE );
			}
			yc = yc.shuffleValues( 0x14 ) * ry;
			yc = yc + FloatVector4( yc ).shuffleValues( 0xEE );
		}
	} else {
		float	uScale = 1.0f / float( w );
		float	vScale = 1.0f / float( h );
		float	uOffset = uScale * 0.5f;
		float	vOffset = vScale * 0.5f;
		for ( int y = y0; y < y1; y++ ) {
			float	yc = float( y ) * vScale + vOffset;
			if ( !( p->textureFlags & 3 ) ) {
				for ( int x = 0; x < w; x++, d++ ) {
					float	xc = float( x ) * uScale + uOffset;
					FloatVector4	c( t.getPixelTC( xc, yc, m ) );
					*d = std::uint32_t( c.shuffleValues( 0xC6 ) * 255.0f );	// RGBA -> BGRA
				}
			} else if ( !( p->textureFlags & 1 ) ) {
				for ( int x = 0; x < w; x++, d++ ) {
					float	xc = float( x ) * uScale + uOffset;
					FloatVector4	c( t.getPixelTC( xc, yc, m ) );
					*d = std::uint32_t( ( c * 0.5f + 0.5f ).shuffleValues( 0xC6 ) * 255.0f );
				}
			} else if ( !( p->textureFlags & 2 ) ) {
				for ( int x = 0; x < w; x++, d++ ) {
					float	xc = float( x ) * uScale + uOffset;
					FloatVector4	c( t.getPixelTC( xc, yc, m ) );
					c[2] = float( std::sqrt( std::max( 0.25f - ( c - 0.5f ).dotProduct2( c - 0.5f ), 0.0f ) ) ) + 0.5f;
					*d = std::uint32_t( c.shuffleValues( 0xC6 ) * 255.0f );
				}
			} else {
				for ( int x = 0; x < w; x++, d++ ) {
					float	xc = float( x ) * uScale + uOffset;
					FloatVector4	c( t.getPixelTC( xc, yc, m ) );
					c[2] = float( std::sqrt( std::max( 1.0f - c.dotProduct2( c ), 0.0f ) ) );
					*d = std::uint32_t( ( c * 0.5f + 0.5f ).shuffleValues( 0xC6 ) * 255.0f );
				}
			}
		}
	}
}

DDSTexturePreview::DDSTexturePreview( QWidget * parent )
	: QWidget( parent )
{
	setSizePolicy( QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding );

	QSettings	settings;
	int	tmp = settings.value( "Settings/UI/Texture Preview Default Size", 512 ).toInt();
	defaultSize = (unsigned short) std::clamp< int >( tmp, 256, 1024 );
}

DDSTexturePreview::~DDSTexturePreview()
{
}

void DDSTexturePreview::setTexture( const DDSTexture16 * txt, bool isNormalMap, bool invertCubeMapZAxis )
{
	t = txt;
	textureFlags = ( !isNormalMap ? 0 : 1 );
	if ( t ) {
		switch ( t->getDXGIFormat() ) {
		case 0x0D:				// DXGI_FORMAT_R16G16B16A16_SNORM
		case 0x1F:				// DXGI_FORMAT_R8G8B8A8_SNORM
		case 0x25:				// DXGI_FORMAT_R16G16_SNORM
		case 0x33:				// DXGI_FORMAT_R8G8_SNORM
		case 0x3A:				// DXGI_FORMAT_R16_SNORM
		case 0x3F:				// DXGI_FORMAT_R8_SNORM
		case 0x51:				// DXGI_FORMAT_BC4_SNORM
		case 0x54:				// DXGI_FORMAT_BC5_SNORM
		case 0x60:				// DXGI_FORMAT_BC6H_SF16
			textureFlags = textureFlags | 2;
			break;
		}
	}
	if ( invertCubeMapZAxis )
		textureFlags = textureFlags | 4;
}

void DDSTexturePreview::paintEvent( [[maybe_unused]] QPaintEvent * e )
{
	double	r = devicePixelRatioF();
	double	w1 = double( width() ) * r;
	double	h1 = double( height() ) * r;
	int	w, h;
	{
		double	a = 1.0;
		if ( t )
			a = ( t->getIsCubeMap() ? 2.0 : double( t->getWidth() ) / double( t->getHeight() ) );
		double	w2 = h1 * a;
		double	h2 = w1 / a;
		w = std::max< int >( int( std::min( w1, w2 ) + 0.5 ), 32 );
		h = std::max< int >( int( std::min( h1, h2 ) + 0.5 ), 32 );
	}

	float	m = 0.0f;
	if ( t ) {
		m = ( float( t->getWidth() ) / float( w ) ) * ( float( t->getHeight() ) / float( h ) );
		if ( t->getIsCubeMap() )
			m = m * 6.0f;
		m = std::clamp( float( std::log2( std::max( m, 1.0f ) ) ) * 0.5f, 0.0f, 16.0f );
	}
	mipLevel = m;

	QImage	img( w, h, QImage::Format_ARGB32 );
	img.setDevicePixelRatio( r );
	std::uint32_t *	imgBuf = reinterpret_cast< std::uint32_t * >( img.bits() );
	{
		std::thread *	threads[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		int	numThreads = int( std::thread::hardware_concurrency() );
		numThreads = std::clamp< int >( std::min< int >( numThreads, h >> 6 ), 1, 8 );
		int	y0 = 0;
		for ( int i = 0; i < numThreads; i++ ) {
			int	y1 = h * ( i + 1 ) / numThreads;
			threads[i] = new std::thread( DDSTexturePreview::threadFunction, this, imgBuf, w, h, y0, y1 );
			y0 = y1;
		}
		for ( int i = 0; i < numThreads; i++ ) {
			threads[i]->join();
			delete threads[i];
		}
	}

	int	xOffs = int( ( w1 - double( w ) ) / ( r * 2.0 ) + 0.5 );
	int	yOffs = int( ( h1 - double( h ) ) / ( r * 2.0 ) + 0.5 );
	QPainter	p( this );
	p.drawImage( QPoint( xOffs, yOffs ), img, img.rect() );
}

QSize DDSTexturePreview::sizeHint() const
{
	int	w = 0;
	int	h = 0;
	if ( t ) {
		w = t->getWidth();
		h = t->getHeight();
		if ( t->getIsCubeMap() ) {
			w = w << 2;
			h = h << 1;
		}
		double	scale = double( defaultSize ) / double( std::max( w, h ) );
		w = int( double( w ) * scale + 0.5 );
		h = int( double( h ) * scale + 0.5 );
	}
	return QSize( std::max< int >( w, 32 ), std::max< int >( h, 32 ) );
}


DDSTextureInfo::DDSTextureInfo(
	Game::GameManager::GameResources & gameResources, const QString & filePath, QWidget * parent )
	: QWidget( parent )
{
	bool	isHDR = ( filePath.endsWith( QLatin1String( ".hdr" ), Qt::CaseInsensitive ) );
	bool	invertCubeZAxis = ( gameResources.game == Game::FALLOUT_4 || gameResources.game == Game::FALLOUT_76 );
	std::string	fullPath = Game::GameManager::get_full_path( filePath, "textures/", ( !isHDR ? ".dds" : ".hdr" ) );
	if ( gameResources.find_file( fullPath ).isEmpty() )
		throw NifSkopeError( "cannot find texture '%s'", filePath.toStdString().c_str() );
	QByteArray	textureData;
	if ( !gameResources.get_file( textureData, fullPath ) )
		throw NifSkopeError( "error opening texture '%s'", fullPath.c_str() );
	qsizetype	fileSize = textureData.size();
	if ( isHDR ) {
		std::vector< unsigned char >	tmpBuf;
		if ( !SFCubeMapCache::convertHDRToDDS( tmpBuf,
												reinterpret_cast< const unsigned char * >( textureData.constData() ),
												size_t( fileSize ), 512, invertCubeZAxis, 65408.0f, 0x43 ) ) {
			throw NifSkopeError( "error converting HDR file '%s'", fullPath.c_str() );
		}
		textureData.resize( qsizetype( tmpBuf.size() ) );
		std::memcpy( textureData.data(), tmpBuf.data(), tmpBuf.size() );
	}
	try {
		t = new DDSTexture16( reinterpret_cast< const unsigned char * >( textureData.constData() ),
								size_t( textureData.size() ), 0, true );

		QGridLayout *	grid = new QGridLayout( this );
		grid->setContentsMargins( 0, 0, 0, 0 );
		grid->setColumnStretch( 0, 0 );
		grid->setColumnStretch( 1, 1 );
		grid->setColumnMinimumWidth( 0, 100 );
		grid->addWidget( new QLabel( tr( "File size" ), this ), 0, 0 );
		grid->addWidget( new QLabel( QString::number( fileSize ), this ), 0, 1 );
		grid->addWidget( new QLabel( tr( "Width" ), this ), 1, 0 );
		grid->addWidget( new QLabel( QString::number( t->getWidth() ), this ), 1, 1 );
		grid->addWidget( new QLabel( tr( "Height" ), this ), 2, 0 );
		grid->addWidget( new QLabel( QString::number( t->getHeight() ), this ), 2, 1 );
		grid->addWidget( new QLabel( tr( "Mipmaps" ), this ), 3, 0 );
		grid->addWidget( new QLabel( QString::number( t->getMaxMipLevel() + 1 ), this ), 3, 1 );
		grid->addWidget( new QLabel( tr( "Num textures" ), this ), 4, 0 );
		grid->addWidget( new QLabel( QString::number( t->getTextureCount() ), this ), 4, 1 );
		grid->addWidget( new QLabel( tr( "Pixel format" ), this ), 5, 0 );
		grid->addWidget( new QLabel( QString( t->getFormatName() ), this ), 5, 1 );
		bool	isNormalMap = ( t->getChannelCount() == 2 && !fullPath.ends_with( "_s.dds" ) );
		textureView = new DDSTexturePreview( this );
		textureView->setTexture( t, isNormalMap, invertCubeZAxis );
		grid->addWidget( textureView, 6, 0, 1, 2 );
	} catch ( ... ) {
		delete t;
		throw;
	}
}

DDSTextureInfo::~DDSTextureInfo()
{
	if ( textureView )
		textureView->setTexture( nullptr, false, false );
	delete t;
}
