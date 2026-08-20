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

#include "uvedit.h"

#include "message.h"
#include "nifskope.h"
#include "gl/glcontext.hpp"
#include "gl/gltex.h"
#include "gl/gltools.h"
#include "model/nifmodel.h"
#include "ui/settingsdialog.h"

#include "libfo76utils/src/fp32vec4.hpp"
#include "libfo76utils/src/filebuf.hpp"
#include "libfo76utils/src/material.hpp"
#include "lib/nvtristripwrapper.h"
#include "io/MeshFile.h"
#include "glview.h"
#include "qt5compat.hpp"

#include <QActionGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPushButton>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QUndoStack> // QUndoCommand Inherited

#define BASESIZE 1024.0
#define GRIDSIZE 16.0
#define GRIDSEGS 4
#define MINZOOM 0.1
#define MAXZOOM 20.0
#define MAXSCALE 10.0
#define MAXTRANS 10.0


UVWidget * UVWidget::createEditor( NifModel * nif, const QModelIndex & idx )
{
	UVWidget * uvw = new UVWidget;
	uvw->setAttribute( Qt::WA_DeleteOnClose );

	if ( !uvw->setNifData( nif, idx ) ) {
		qCWarning( nsSpell ) << tr( "Could not load texture data for UV editor." );
		delete uvw;
		return nullptr;
	}

	uvw->showMaximized();
	return uvw;
}

static GLdouble glUnit  = ( 1.0 / BASESIZE );

QStringList UVWidget::texnames = {
	"Base Texture", "Dark Texture", "Detail Texture",
	"Gloss Texture", "Glow Texture", "Bump Map Texture",
	"Decal 0 Texture", "Decal 1 Texture", "Decal 2 Texture",
	"Decal 3 Texture"
};


UVWidget::UVWidget( QWidget * parent )
	: QOpenGLWidget( parent, ( QSettings().value( "Settings/UI/UV Editor Window Stays on Top", false ).toBool() ?
								Qt::Window | Qt::WindowStaysOnTopHint : Qt::Window ) ),
		undoStack( new QUndoStack( this ) )
{
	cx = nullptr;
	{
		QSettings settings;
		int	aa = settings.value( "Settings/Render/General/Msaa Samples", 2 ).toInt();
		aa = std::min< int >( std::max< int >( aa, 0 ), 4 );

		QSurfaceFormat	fmt = format();
		// OpenGL version (4.1 or 4.2, core profile)
		fmt.setRenderableType( QSurfaceFormat::OpenGL );
		fmt.setMajorVersion( 4 );
#ifdef Q_OS_MACOS
		fmt.setMinorVersion( 1 );
#else
		fmt.setMinorVersion( 2 );
#endif
		fmt.setProfile( QSurfaceFormat::CoreProfile );
		fmt.setOption( QSurfaceFormat::DeprecatedFunctions, false );

		// V-Sync
		fmt.setSwapInterval( 1 );
		fmt.setSwapBehavior( QSurfaceFormat::DoubleBuffer );

		fmt.setDepthBufferSize( 24 );
		fmt.setStencilBufferSize( 8 );
		fmt.setSamples( 1 << aa );

		setFormat( fmt );
		setTextureFormat( GL_SRGB8 );
	}

	setWindowTitle( tr( "UV Editor" ) );
	setFocusPolicy( Qt::StrongFocus );

	textures = new TexCache( this );

	zoom = 1.2;

	pos[0] = 0.0;
	pos[1] = 0.0;

	mousePos = QPoint( -1000, -1000 );

	setCursor( QCursor( Qt::CrossCursor ) );
	setMouseTracking( true );

	setContextMenuPolicy( Qt::ActionsContextMenu );

	QAction * aUndo = undoStack->createUndoAction( this );
	QAction * aRedo = undoStack->createRedoAction( this );

	aUndo->setShortcut( QKeySequence::Undo );
	aRedo->setShortcut( QKeySequence::Redo );

	addAction( aUndo );
	addAction( aRedo );

	QAction * aSep = new QAction( this );
	aSep->setSeparator( true );
	addAction( aSep );

	QAction * aSelectAll = new QAction( tr( "Select &All" ), this );
	aSelectAll->setShortcut( QKeySequence::SelectAll );
	connect( aSelectAll, &QAction::triggered, this, &UVWidget::selectAll );
	addAction( aSelectAll );

	QAction * aSelectNone = new QAction( tr( "Select &None" ), this );
	connect( aSelectNone, &QAction::triggered, this, &UVWidget::selectNone );
	addAction( aSelectNone );

	QAction * aSelectFaces = new QAction( tr( "Select &Faces" ), this );
	connect( aSelectFaces, &QAction::triggered, this, &UVWidget::selectFaces );
	addAction( aSelectFaces );

	QAction * aSelectConnected = new QAction( tr( "Select &Connected" ), this );
	connect( aSelectConnected, &QAction::triggered, this, &UVWidget::selectConnected );
	addAction( aSelectConnected );

	QAction * aScaleSelection = new QAction( tr( "&Scale and Translate Selected" ), this );
	aScaleSelection->setShortcut( QKeySequence( "Alt+S" ) );
	connect( aScaleSelection, &QAction::triggered, this, &UVWidget::scaleSelection );
	addAction( aScaleSelection );

	QAction * aRotateSelection = new QAction( tr( "&Rotate Selected" ), this );
	aRotateSelection->setShortcut( QKeySequence( "Alt+R" ) );
	connect( aRotateSelection, &QAction::triggered, this, &UVWidget::rotateSelection );
	addAction( aRotateSelection );

	QAction * aCopyTexCoord = new QAction( tr( "Copy to Clipboard" ), this );
	connect( aCopyTexCoord, &QAction::triggered, this, &UVWidget::copyTexCoord );
	addAction( aCopyTexCoord );

	QAction * aPasteTexCoord = new QAction( tr( "Paste from Clipboard" ), this );
	connect( aPasteTexCoord, &QAction::triggered, this, &UVWidget::pasteTexCoord );
	addAction( aPasteTexCoord );

	aSep = new QAction( this );
	aSep->setSeparator( true );
	addAction( aSep );

	aTextureBlend = new QAction( tr( "Texture Alpha Blending" ), this );
	aTextureBlend->setCheckable( true );
	aTextureBlend->setChecked( true );
	connect( aTextureBlend, &QAction::toggled, this, &UVWidget::update_Blend );
	addAction( aTextureBlend );

	updateSettings();

	connect( NifSkope::getOptions(), &SettingsDialog::saveSettings, this, &UVWidget::updateSettings );
	connect( NifSkope::getOptions(), &SettingsDialog::update3D, this, &UVWidget::update_3D );
}

UVWidget::~UVWidget()
{
	QOpenGLContext *	prvContext = QOpenGLContext::currentContext();
	if ( context() != prvContext )
		makeCurrent();

	delete textures;
	nif = nullptr;
	delete cx;

	if ( !prvContext )
		doneCurrent();
	else if ( prvContext != context() )
		prvContext->makeCurrent( prvContext->surface() );
}

void UVWidget::updateSettings()
{
	QSettings settings;
	settings.beginGroup( "Settings/Render/Colors/" );

	cfg.background = Color4( settings.value( "Background" ).value<QColor>() );
	cfg.highlight = Color4( settings.value( "Highlight" ).value<QColor>() );
	cfg.wireframe = Color4( settings.value( "Wireframe" ).value<QColor>() );

	settings.endGroup();
}

void UVWidget::initializeGL()
{
	cx = new NifSkopeOpenGLContext( context() );
	textures->setOpenGLContext( cx );
	cx->updateShaders();

	glEnable( GL_MULTISAMPLE );

	glEnable( GL_BLEND );
	cx->fn->glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );

	glDepthFunc( GL_LEQUAL );
	glEnable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
	glDisable( GL_FRAMEBUFFER_SRGB );
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

	glClearColor( cfg.background[0], cfg.background[1], cfg.background[2], cfg.background[3] );

	// check for errors
	GLenum err;

	while ( ( err = glGetError() ) != GL_NO_ERROR ) {
		qDebug() << "GL ERROR (init) : " << GLView::getGLErrorString( int(err) );
	}
}

void UVWidget::resizeGL( int width, int height )
{
	double	p = devicePixelRatioF();
	pixelWidth = int( p * width + 0.5 );
	pixelHeight = int( p * height + 0.5 );
	updateViewRect( pixelWidth, pixelHeight );
}

void UVWidget::paintGL()
{
	if ( !cx )
		return;
	cx->setCacheSize( 16777216 );
	cx->setViewport( 0, 0, pixelWidth, pixelHeight );

	FloatVector4	bgColor = FloatVector4( cfg.background );

	glDepthMask( GL_TRUE );

	auto	prog = cx->useProgram( "uvedit.prog" );
	if ( !( textures && prog && nif ) ) {
		glClearColor( bgColor[0], bgColor[1], bgColor[2], bgColor[3] );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		return;
	}

	glClear( GL_DEPTH_BUFFER_BIT );

	static const float	positions[8] = { 0.0f, 0.0f,  0.0f, 1.0f,  1.0f, 0.0f,  1.0f, 1.0f };
	static const float *	attrData[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, positions };
	cx->bindShape( 4, 0x20000000, 0, attrData, nullptr );

	textures->activateTextureUnit( 0 );
	FloatVector4	uvScaleAndOffset( 1.0f, 1.0f, 0.0f, 0.0f );
	int	textureColorMode = -1;
	if ( currentTexFile >= 0 && currentTexFile < texfiles.size() && !texfiles[currentTexFile].name.isEmpty() ) {
		const TextureInfo &	t = texfiles.at( currentTexFile );
		if ( bindTexture( t ) ) {
			uvScaleAndOffset = t.scaleAndOffset;
			textureColorMode = t.colorMode & 3;
		}
	} else if ( bindTexture( texsource ) ) {
		textureColorMode = 0;
	}
	if ( textureColorMode < 0 ) {
		static const QString	defaultTexture = "#FFFFFFFF";
		textures->bind( defaultTexture, nif );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
		textureColorMode = 0;
	}

	prog->uni4f( "viewScaleAndOffset", viewScaleAndOffset );

	prog->uni1i( "BaseMap", 0 );
	prog->uni1i( "textureColorMode", textureColorMode | ( aTextureBlend->isChecked() ? 0 : 4 ) );
	prog->uni2f( "uvCenter", 0.0f, 0.0f );
	prog->uni4f( "uvScaleAndOffset", uvScaleAndOffset );
	prog->uni1f( "uvRotation", 0.0f );
	float	pixelScaleX = float( pixelWidth ) / viewScaleAndOffset[0];
	float	pixelScaleY = float( pixelHeight ) / viewScaleAndOffset[1];
	prog->uni2f( "pixelScale", pixelScaleX, pixelScaleY );
	FloatVector4	gridColors[3] = {
		FloatVector4( 1.0f, 1.0f, 1.0f, 0.4f ),
		FloatVector4( 1.0f, 1.0f, 1.0f, 0.2f ),
		FloatVector4( 1.0f, 1.0f, 1.0f, 0.1f )
	};
	float	gridLineWidths[3] = {
		GLView::Settings::lineWidthGrid,
		GLView::Settings::lineWidthGrid * ( 6.0f / 7.0f ),
		GLView::Settings::lineWidthGrid * ( 4.0f / 7.0f )
	};
	for ( int i = 0; i < 3; i++ ) {
		if ( gridLineWidths[i] < 1.0f ) {
			gridColors[i][3] *= gridLineWidths[i];
			gridLineWidths[i] = 1.0f;
		}
	}
	prog->uni4fv( "gridColors", gridColors, 3 );
	prog->uni3f( "gridLineWidths", gridLineWidths[0], gridLineWidths[1], gridLineWidths[2] );
	bool	gridEnabled[3] = { true, ( zoom <= ( GRIDSEGS * GRIDSEGS / 2.0 ) ), ( zoom <= ( GRIDSEGS / 2.0 ) ) };
	prog->uni1bv( "gridEnabled", gridEnabled, 3 );
	prog->uni4f( "backgroundColor", bgColor );
	prog->uni2f( "textureColorScale", 0.75f, 0.5f );

	glDisable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );
	glDisable( GL_CULL_FACE );
	glDisable( GL_BLEND );
	glDisable( GL_FRAMEBUFFER_SRGB );

	// draw texture and grid
	cx->fn->glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

	drawTexCoords();

	// draw selection
	if ( ( !selectRect.isNull() || selectPoly.size() > 1 ) && ( prog = cx->useProgram( "lines.prog" ) ) != nullptr ) {
		glEnable( GL_BLEND );
		cx->fn->glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );

		prog->uni4f( "vertexColorOverride", FloatVector4( 1.0e-15f ).maxValues( FloatVector4( cfg.highlight ) ) );
		prog->uni1i( "selectionParam", -1 );
		prog->uni1f( "lineWidth", GLView::Settings::lineWidthWireframe * 0.5f );

		qsizetype	numVerts = std::max< qsizetype >( selectPoly.size(), 4 );
		if ( vertexPosBuf.size() < numVerts )
			vertexPosBuf.resize( numVerts );
		Vector3 *	positions = vertexPosBuf.data();
		const float *	p = &( positions[0][0] );

		if ( !selectRect.isNull() ) {
			positions[0] = Vector3( mapToContents( selectRect.topLeft() ), 1.0f );
			positions[1] = Vector3( mapToContents( selectRect.bottomLeft() ), 1.0f );
			positions[2] = Vector3( mapToContents( selectRect.bottomRight() ), 1.0f );
			positions[3] = Vector3( mapToContents( selectRect.topRight() ), 1.0f );

			cx->bindShape( 4, 0x03, 0, &p, nullptr );

			cx->fn->glDrawArrays( GL_LINE_LOOP, 0, 4 );
		}

		if ( selectPoly.size() > 1 ) {
			for ( qsizetype i = 0; i < selectPoly.size(); i++ )
				positions[i] = Vector3( mapToContents( selectPoly.at( i ) ), 1.0f );

			cx->bindShape( (unsigned int) selectPoly.size(), 0x03, 0, &p, nullptr );

			cx->fn->glDrawArrays( GL_LINE_LOOP, 0, GLsizei( selectPoly.size() ) );
		}
	}

	cx->shrinkCache();
}

void UVWidget::drawTexCoords()
{
	qsizetype	numVerts = texcoords.size();
	qsizetype	numTriangles = faces.size();
	if ( numVerts < 1 || numTriangles < 1 )
		return;

	vertexPosBuf.resize( numVerts );
	vertexColorBuf.resize( numVerts );

	Vector3 *	vertexPosData = vertexPosBuf.data();
	FloatVector4 *	vertexColorData = vertexColorBuf.data();

	FloatVector4	nlColor = FloatVector4( Color4(cfg.wireframe) ).blendValues( FloatVector4( 0.5f ), 0x08 );
	FloatVector4	hlColor = FloatVector4( Color4(cfg.highlight) ).blendValues( FloatVector4( 0.5f ), 0x08 );

	// load geometry data
	for ( qsizetype i = 0; i < numVerts; i++ ) {
		Vector2	v( texcoords.at( i ) );
		float	z = 0.0f;
		if ( selection.contains( int( i ) ) ) {
			vertexColorData[i] = hlColor;
			z = 1.0f;
		} else {
			vertexColorData[i] = nlColor;
		}
		vertexPosData[i] = Vector3( v[0], v[1], z );
	}

	const float *	attrData[2] = { &( vertexPosData[0][0] ), &( vertexColorData[0][0] ) };
	cx->bindShape( (unsigned int) numVerts, 0x43, size_t( numTriangles ) * 6, attrData, faces.constData() );

	glEnable( GL_BLEND );
	cx->fn->glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );

	glDepthFunc( GL_LEQUAL );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDisable( GL_CULL_FACE );
	glDisable( GL_FRAMEBUFFER_SRGB );

	// draw triangle edges
	if ( auto prog = cx->useProgram( "wireframe.prog" ); prog ) {
		prog->uni4f( "vertexColorOverride", FloatVector4( 0.0f ) );
		prog->uni1i( "selectionParam", -1 );
		prog->uni1f( "lineWidth", GLView::Settings::lineWidthWireframe * 0.625f );

		cx->fn->glDrawElements( GL_TRIANGLES, GLsizei( numTriangles * 3 ), GL_UNSIGNED_SHORT, (void *) 0 );
	}

	// draw points
	if ( auto prog = cx->useProgram( "selection.prog" ); prog ) {
		prog->uni4f( "vertexColorOverride", FloatVector4( 0.0f ) );
		float	pointSize = GLView::Settings::vertexPointSize * 0.75f + 0.5f;
		prog->uni1i( "selectionFlags", ( roundFloat( std::min( pointSize * 8.0f, 255.0f ) ) << 8 ) | 0x0002 );
		prog->uni1i( "selectionParam", -1 );
		glPointSize( pointSize );

		cx->fn->glDrawArrays( GL_POINTS, 0, GLsizei( numVerts ) );
	}
}

void UVWidget::updateViewRect( int width, int height )
{
	double	scaleX, scaleY;
	if ( width < height ) {
		scaleX = zoom;
		scaleY = zoom * double( height ) / double( width );
	} else {
		scaleX = zoom * double( width ) / double( height );
		scaleY = zoom;
	}
	double	offsX = pos[0] + 0.5 - scaleX * 0.5;
	double	offsY = pos[1] + 0.5 - scaleY * 0.5;
	viewScaleAndOffset = FloatVector4( float( scaleX ), float( scaleY ), float( offsX ), float( offsY ) );

	cx->setProjectionMatrix( Matrix4() );
	cx->setGlobalUniforms();

	cx->setDefaultVertexAttribs( Scene::defaultAttrMask, Scene::defaultVertexAttrs );

	Matrix4	modelViewMatrix;
	double	invScaleX = 2.0 / scaleX;
	double	invScaleY = 2.0 / scaleY;
	modelViewMatrix( 0, 0 ) = float( invScaleX );
	modelViewMatrix( 1, 1 ) = float( -invScaleY );
	modelViewMatrix( 2, 2 ) = -0.25f;
	modelViewMatrix( 3, 0 ) = float( ( pos[0] + 0.5 ) * -invScaleX );
	modelViewMatrix( 3, 1 ) = float( ( pos[1] + 0.5 ) * invScaleY );
	modelViewMatrix( 3, 2 ) = 0.375f;

	if ( auto prog = cx->useProgram( "lines.prog" ); prog ) {
		prog->uni4m( "modelViewMatrix", modelViewMatrix );
	}
	if ( auto prog = cx->useProgram( "selection.prog" ); prog ) {
		prog->uni4m( "modelViewMatrix", modelViewMatrix );
	}
	if ( auto prog = cx->useProgram( "wireframe.prog" ); prog ) {
		prog->uni3m( "normalMatrix", Matrix() );
		prog->uni4m( "modelViewMatrix", modelViewMatrix );
	}
}

QPoint UVWidget::mapFromContents( const Vector2 & v ) const
{
	double	x = ( double( v[0] ) - double( viewScaleAndOffset[2] ) ) / double( viewScaleAndOffset[0] );
	double	y = ( double( v[1] ) - double( viewScaleAndOffset[3] ) ) / double( viewScaleAndOffset[1] );
	x *= double( pixelWidth );
	y *= double( pixelHeight );

	return QPointF( x, y ).toPoint();
}

Vector2 UVWidget::mapToContents( const QPoint & p ) const
{
	double	x = double( p.x() ) / double( pixelWidth );
	double	y = double( p.y() ) / double( pixelHeight );
	x = x * double( viewScaleAndOffset[0] ) + double( viewScaleAndOffset[2] );
	y = y * double( viewScaleAndOffset[1] ) + double( viewScaleAndOffset[3] );

	return Vector2( float( x ), float( y ) );
}

QVector<int> UVWidget::indices( const QPoint & p ) const
{
	int	d = int( devicePixelRatioF() * 5.0 + 0.5 );
	int	d2 = d >> 1;
	return indices( QRect( p - QPoint( d2, d2 ), QSize( d, d ) ) );
}

QVector<int> UVWidget::indices( const QRegion & region ) const
{
	QList<int> hits;

	for ( int i = 0; i < texcoords.count(); i++ ) {
		if ( region.contains( mapFromContents( texcoords[ i ] ) ) )
			hits << i;
	}

	return hits.toVector();
}

bool UVWidget::bindTexture( const TextureInfo & t )
{
	GLuint mipmaps = 0;
	mipmaps = textures->bind( t.name, nif );

	if ( mipmaps ) {
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR );
		if ( t.clampMode == 0 ) {
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
		} else if ( t.clampMode != 2 ) {
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		} else {
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT );
		}

		// TODO: Add support for non-square textures

		return true;
	}

	return false;
}

bool UVWidget::bindTexture( const QModelIndex & iSource )
{
	GLuint mipmaps = 0;
	mipmaps = textures->bind( iSource );

	if ( mipmaps ) {
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );

		// TODO: Add support for non-square textures

		return true;
	}

	return false;
}


QSize UVWidget::sizeHint() const
{
	if ( sHint.isValid() ) {
		return sHint;
	}

	return QSizeF( BASESIZE, BASESIZE ).toSize();
}

void UVWidget::setSizeHint( const QSize & s )
{
	sHint = s;
}

QSize UVWidget::minimumSizeHint() const
{
	return QSizeF( BASESIZE, BASESIZE ).toSize();
}

int UVWidget::heightForWidth( int width ) const
{
	if ( width < minimumSizeHint().height() )
		return minimumSizeHint().height();

	return width;
}

void UVWidget::mousePressEvent( QMouseEvent * e )
{
	double	p = devicePixelRatioF();
	QPoint	pixelPos( ( getQMouseEventPosition( e ) * p ).toPoint() );
	QPoint	dPos( pixelPos - mousePos );
	mousePos = pixelPos;

	if ( e->button() == Qt::LeftButton ) {
		QVector<int> hits = indices( mousePos );

		if ( hits.isEmpty() ) {
			if ( !e->modifiers().testFlag( Qt::ShiftModifier ) )
				selectNone();

			if ( e->modifiers().testFlag( Qt::AltModifier ) || e->modifiers().testFlag( Qt::ControlModifier ) ) {
				selectPoly << pixelPos;
			} else {
				selectRect.setTopLeft( mousePos );
				selectRect.setBottomRight( mousePos );
			}
		} else {
			if ( dPos.manhattanLength() > int( p * 4.0 + 0.5 ) )
				selectCycle = 0;
			else
				selectCycle++;

			int h = hits[ selectCycle % (unsigned int) hits.count() ];

			if ( !e->modifiers().testFlag( Qt::ShiftModifier ) ) {
				if ( !isSelected( h ) )
					selectNone();

				select( h );
			} else {
				select( h, !isSelected( h ) );
			}

			if ( selection.isEmpty() ) {
				setCursor( QCursor( Qt::CrossCursor ) );
			} else {
				setCursor( QCursor( Qt::SizeAllCursor ) );
			}
		}
	}

	update();
}

void UVWidget::mouseMoveEvent( QMouseEvent * e )
{
	double	p = devicePixelRatioF();
	QPoint	pixelPos( ( getQMouseEventPosition( e ) * p ).toPoint() );
	QPoint	dPos( pixelPos - mousePos );

	switch ( e->buttons() ) {
	case Qt::LeftButton:

		if ( !selectRect.isNull() ) {
			selectRect.setBottomRight( pixelPos );
		} else if ( !selectPoly.isEmpty() ) {
			selectPoly << pixelPos;
		} else {
			auto dPosX = dPos.x();
			auto dPosY = dPos.y();

			if ( kbd[Qt::Key_X] )
				dPosY = 0.0;
			if ( kbd[Qt::Key_Y] )
				dPosX = 0.0;

			if ( e->modifiers().testFlag( Qt::ControlModifier ) )
				rotate_Selection( ( dPosX + dPosY ) * 0.5f / float( p ) );
			else
				moveSelection( dPosX * glUnit * zoom, dPosY * glUnit * zoom );
		}
		break;

	case Qt::MiddleButton:
		pos[0] -= dPos.x() * double( viewScaleAndOffset[0] ) / double( pixelWidth );
		pos[1] -= dPos.y() * double( viewScaleAndOffset[1] ) / double( pixelHeight );
		updateViewRect( pixelWidth, pixelHeight );

		setCursor( QCursor( Qt::ClosedHandCursor ) );

		break;

	case Qt::RightButton:
		// FIXME: this does not work on Linux because the right button activates the menu instead
		zoom *= std::pow( GLView::Settings::zoomInScale, double( dPos.y() ) / ( p * 4.0 ) );

		zoom = std::min< double >( std::max< double >( zoom, MINZOOM ), MAXZOOM );

		updateViewRect( pixelWidth, pixelHeight );

		setCursor( QCursor( Qt::SizeVerCursor ) );

		break;

	default:

		if ( indices( pixelPos ).count() ) {
			setCursor( QCursor( Qt::PointingHandCursor ) );
		} else {
			setCursor( QCursor( Qt::CrossCursor ) );
		}
		return;
	}

	mousePos = pixelPos;
	update();
}

void UVWidget::mouseReleaseEvent( QMouseEvent * e )
{
	double	p = devicePixelRatioF();
	QPoint	pixelPos( ( getQMouseEventPosition( e ) * p ).toPoint() );

	switch ( e->button() ) {
	case Qt::LeftButton:

		if ( !selectRect.isNull() ) {
			select( QRegion( selectRect.normalized() ) );
			selectRect = QRect();
		} else if ( !selectPoly.isEmpty() ) {
			if ( selectPoly.size() > 2 ) {
				select( QRegion( QPolygon( selectPoly.toVector() ) ) );
			}

			selectPoly.clear();
		}

		break;
	default:
		break;
	}

	if ( indices( pixelPos ).count() ) {
		setCursor( QCursor( Qt::ArrowCursor ) );
	} else {
		setCursor( QCursor( Qt::CrossCursor ) );
	}

	update();
}

void UVWidget::wheelEvent( QWheelEvent * e )
{
	switch ( e->modifiers() ) {
	case Qt::NoModifier:
		zoom *= std::pow( GLView::Settings::zoomInScale, double( e->angleDelta().y() ) / 60.0 );

		zoom = std::min< double >( std::max< double >( zoom, MINZOOM ), MAXZOOM );

		updateViewRect( pixelWidth, pixelHeight );

		break;
	}

	update();
}

void UVWidget::keyPressEvent( QKeyEvent * e )
{
	switch ( e->key() ) {
	case Qt::Key_X:
	case Qt::Key_Y:
		kbd[e->key()] = true;
		break;
	default:
		e->ignore();
		break;
	}
}

void UVWidget::keyReleaseEvent( QKeyEvent * e )
{
	switch ( e->key() ) {
	case Qt::Key_X:
	case Qt::Key_Y:
		kbd[e->key()] = false;
		break;
	default:
		e->ignore();
		break;
	}
}

UVWidget::TextureInfo::TextureInfo( const NifModel * nif, const QString & texturePath )
{
	clampMode = 0;	// Wrap
	colorMode = 0;
	scaleAndOffset = FloatVector4( 1.0f, 1.0f, 0.0f, 0.0f );
	name = TexCache::find( texturePath, nif );
}

UVWidget::TextureInfo::TextureInfo( const NifModel * nif, const std::string_view * texturePath, const void * uvStream )
{
	if ( !uvStream )
		uvStream = &CE2Material::defaultUVStream;
	name = TexCache::find( QString::fromLatin1( texturePath->data(), qsizetype( texturePath->length() ) ), nif );
	clampMode = reinterpret_cast< const CE2Material::UVStream * >( uvStream )->textureAddressMode & 3;
	colorMode = 0;
	scaleAndOffset = reinterpret_cast< const CE2Material::UVStream * >( uvStream )->scaleAndOffset;
}

void UVWidget::setTexturePaths( NifModel * nif, QModelIndex iTexProp )
{
	if ( !iTexProp.isValid() )
		return;

	const QString &	blockType = nif->getItem( iTexProp )->name();

	if ( !( blockType == "BSLightingShaderProperty" || blockType == "BSEffectShaderProperty" ) )
		return;

	QModelIndex	iTexPropData;
	if ( nif->getBSVersion() >= 151 )
		iTexPropData = nif->getIndex( iTexProp, "Material" );
	if ( iTexPropData.isValid() ) {
		// using external material file
		if ( nif->getBSVersion() < 170 ) {
			// Fallout 76
			for ( int texSlot = 0; texSlot <= 9; texSlot++ ) {
				TextureInfo	t( nif, nif->get<QString>( iTexPropData, QString( "Texture %1" ).arg( texSlot ) ) );
				if ( t.name.isEmpty() )
					continue;
				t.clampMode = int( ( nif->get<quint16>( iTexPropData, "Shader Flags 1" ) & 3 ) == 0 );
				if ( texSlot == 0 || texSlot == 5 || texSlot == 6 )
					t.colorMode = 1;
				else if ( texSlot == 1 )
					t.colorMode = 3;
				Vector2	uvOffset = nif->get<Vector2>( iTexPropData, "UV Offset" );
				Vector2	uvScale = nif->get<Vector2>( iTexPropData, "UV Scale" );
				t.scaleAndOffset = FloatVector4( uvScale[0], uvScale[1], uvOffset[0], uvOffset[1] );
				texfiles.append( t );
			}
		} else {
			// Starfield
			AllocBuffers	sfMatBuf;
			auto	matData = reinterpret_cast< const CE2Material * >( nif->updateSFMaterial( sfMatBuf, iTexProp ) );
			if ( !matData )
				return;
			for ( size_t i = 0; i < CE2Material::maxLayers; i++ ) {
				if ( !( matData->layerMask & ( 1U << i ) ) )
					continue;
				if ( !( matData->layers[i] && matData->layers[i]->material && matData->layers[i]->material->textureSet ) )
					continue;
				const CE2Material::TextureSet *	txtSet = matData->layers[i]->material->textureSet;
				std::uint32_t	texPathMask = txtSet->texturePathMask;
				if ( !texPathMask )
					continue;
				for ( size_t j = 0; texPathMask && j < CE2Material::TextureSet::maxTexturePaths; j++, texPathMask = texPathMask >> 1 ) {
					if ( !( texPathMask & 1 ) || txtSet->texturePaths[j]->empty() )
						continue;
					const CE2Material::UVStream *	uvStream = matData->layers[i]->uvStream;
					if ( j == 2 && ( matData->flags & CE2Material::Flag_HasOpacity ) && i == matData->alphaSourceLayer )
						uvStream = matData->alphaUVStream;
					TextureInfo	t( nif, txtSet->texturePaths[j], uvStream );
					if ( j == 0 || j == 7 || j == 14 )
						t.colorMode = 1;
					else if ( j == 1 )
						t.colorMode = 3;
					if ( !t.name.isEmpty() )
						texfiles.append( t );
				}
				if ( i > 0 && i <= CE2Material::maxBlenders && matData->blenders[i - 1] ) {
					const CE2Material::Blender *	blender = matData->blenders[i - 1];
					if ( !blender->texturePath->empty() ) {
						TextureInfo	t( nif, blender->texturePath, blender->uvStream );
						if ( !t.name.isEmpty() )
							texfiles.append( t );
					}
				}
			}
			if ( ( matData->flags & CE2Material::Flag_UseDetailBlender ) && matData->detailBlenderSettings ) {
				const CE2Material::DetailBlenderSettings *	sp = matData->detailBlenderSettings;
				if ( sp->isEnabled && !sp->texturePath->empty() ) {
					TextureInfo	t( nif, sp->texturePath, sp->uvStream );
					if ( !t.name.isEmpty() )
						texfiles.append( t );
				}
			}
		}
		return;
	}

	if ( nif->getBSVersion() >= 151 )
		iTexPropData = nif->getIndex( iTexProp, "Shader Property Data" );
	else
		iTexPropData = iTexProp;
	if ( !iTexPropData.isValid() )
		return;
	Vector2	uvOffset = nif->get<Vector2>( iTexPropData, "UV Offset" );
	Vector2	uvScale = nif->get<Vector2>( iTexPropData, "UV Scale" );
	int	clampMode = 0;
	if ( nif->getBSVersion() < 151 )
		clampMode = int( ( nif->get<quint32>( iTexPropData, "Texture Clamp Mode" ) & 3 ) == 0 );
	if ( blockType == "BSLightingShaderProperty" ) {
		QModelIndex iTexSource = nif->getBlockIndex( nif->getLink( iTexPropData, "Texture Set" ) );
		QModelIndex	iTextures;

		if ( iTexSource.isValid() && ( iTextures = nif->getIndex( iTexSource, "Textures" ) ).isValid() ) {
			int	texSlots = nif->rowCount( iTextures );
			for ( int texSlot = 0; texSlot < texSlots; texSlot++ ) {
				TextureInfo	t( nif, nif->get<QString>( nif->getIndex( iTextures, texSlot ) ) );
				if ( t.name.isEmpty() )
					continue;
				t.clampMode = clampMode;
				if ( ( texSlot == 0 || texSlot == 9 ) && nif->getBSVersion() >= 151 )
					t.colorMode = 1;
				else if ( texSlot == 1 && nif->getBSVersion() >= 130 )
					t.colorMode = ( nif->getBSVersion() < 151 ? 2 : 3 );
				t.scaleAndOffset = FloatVector4( uvScale[0], uvScale[1], uvOffset[0], uvOffset[1] );
				texfiles.append( t );
			}
		}
	} else {
		for ( int texSlot = 0; texSlot <= 1; texSlot++ ) {
			QModelIndex	iTexturePath = nif->getIndex( iTexPropData, ( texSlot == 0 ? "Source Texture" : "Normal Texture" ) );
			if ( !iTexturePath.isValid() )
				continue;
			TextureInfo	t( nif, nif->get<QString>( iTexturePath ) );
			if ( t.name.isEmpty() )
				continue;
			t.clampMode = clampMode;
			if ( texSlot == 0 && nif->getBSVersion() >= 151 )
				t.colorMode = 1;
			else if ( nif->getBSVersion() >= 130 )
				t.colorMode = ( nif->getBSVersion() < 151 ? 2 : 3 );
			t.scaleAndOffset = FloatVector4( uvScale[0], uvScale[1], uvOffset[0], uvOffset[1] );
			texfiles.append( t );
		}
	}
}

static QString getTES4NormalOrGlowMap( const NifModel * nif, const QModelIndex & iTexProp, int n )
{
	do {
		if ( !nif->get<bool>( iTexProp, "Has Base Texture" ) )
			break;
		QModelIndex	i = nif->getIndex( iTexProp, "Base Texture" );
		if ( !i.isValid() )
			break;
		i = nif->getBlockIndex( nif->getLink( i, "Source" ) );
		if ( !i.isValid() )
			break;
		i = nif->getIndex( i, "File Name" );
		if ( !i.isValid() )
			break;
		QString	texturePath = nif->get<QString>( i );
		if ( !texturePath.endsWith( ".dds", Qt::CaseInsensitive ) )
			break;
		texturePath.chop( 4 );
		texturePath.append( n == 4 ? "_g.dds" : "_n.dds" );
		return nif->findResourceFile( TexCache::find( texturePath, nif ), nullptr, nullptr );
	} while ( false );
	return QString();
}

bool UVWidget::setNifData( NifModel * nifModel, const QModelIndex & nifIndex )
{
	if ( nif ) {
		// disconnect( nif ) may not work with new Qt5 syntax...
		// it says the calls need to remain symmetric to the connect() ones.
		// Otherwise, use QMetaObject::Connection
		//disconnect( nif );
		this->disconnect();
	}

	undoStack->clear();

	nif = nifModel;
	iShape = nifIndex;
	isDataOnSkin = false;
	sfMeshIndex = QModelIndex();

	auto newTitle = tr("UV Editor");
	if (nif)
		newTitle += tr(" - ") + nif->getFileInfo().fileName();
	setWindowTitle(newTitle);

	// Version dependent actions
	if ( nif && nif->getVersionNumber() != 0x14020007 ) {
		coordSetGroup = new QActionGroup( this );
		connect( coordSetGroup, &QActionGroup::triggered, this, &UVWidget::selectCoordSet );

		coordSetSelect = new QMenu( tr( "Select Coordinate Set" ) );
		addAction( coordSetSelect->menuAction() );
		connect( coordSetSelect, &QMenu::aboutToShow, this, &UVWidget::getCoordSets );
	}

	texSlotGroup = new QActionGroup( this );
	connect( texSlotGroup, &QActionGroup::triggered, this, &UVWidget::selectTexSlot );

	menuTexSelect = new QMenu( tr( "Select Texture Slot" ) );
	addAction( menuTexSelect->menuAction() );
	connect( menuTexSelect, &QMenu::aboutToShow, this, &UVWidget::getTexSlots );

	if ( nif ) {
		connect( nif, &NifModel::modelReset, this, &UVWidget::close );
		connect( nif, &NifModel::destroyed, this, &UVWidget::close );
		connect( nif, &NifModel::dataChanged, this, &UVWidget::nifDataChanged );
		connect( nif, &NifModel::rowsRemoved, this, &UVWidget::nifDataChanged );
	}

	if ( !nif )
		return false;

	textures->setNifFolder( nif->getFolder() );

	iShapeData = nif->getBlockIndex( nif->getLink( iShape, "Data" ) );
	if ( nif->getVersionNumber() == 0x14020007 && nif->getBSVersion() >= 100 ) {
		iShapeData = nif->getIndex( iShape, "Vertex Data" );

		auto vf = nif->get<BSVertexDesc>( iShape, "Vertex Desc" );
		if ( (vf & VertexFlags::VF_SKINNED) && nif->getBSVersion() == 100 ) {
			// Skinned SSE
			auto skinID = nif->getLink( nif->getIndex( iShape, "Skin" ) );
			auto partID = nif->getLink( nif->getBlockIndex( skinID, "NiSkinInstance" ), "Skin Partition" );
			iPartBlock = nif->getBlockIndex( partID, "NiSkinPartition" );
			if ( !iPartBlock.isValid() )
				return false;

			isDataOnSkin = true;

			iShapeData = nif->getIndex( iPartBlock, "Vertex Data" );
		}
	}

	if ( nif->blockInherits( iShapeData, "NiTriBasedGeomData" ) ) {
		iTexCoords = nif->getIndex( nif->getIndex( iShapeData, "UV Sets" ), 0 );

		if ( !iTexCoords.isValid() || !nif->rowCount( iTexCoords ) ) {
			return false;
		}

		if ( !setTexCoords() ) {
			return false;
		}
	} else if ( nif->blockInherits( iShape, "BSTriShape" ) ) {
		int numVerts = 0;
		if ( !isDataOnSkin )
			numVerts = nif->get<int>( iShape, "Num Vertices" );
		else
			numVerts = nif->get<uint>( iPartBlock, "Data Size" ) / nif->get<uint>( iPartBlock, "Vertex Size" );

		for ( int i = 0; i < numVerts; i++ ) {
			texcoords << nif->get<Vector2>( nif->index( i, 0, iShapeData ), "UV" );
		}

		// Fake index so that isValid() checks do not fail
		iTexCoords = iShape;

		if ( !setTexCoords() )
			return false;
	} else if ( nif->getBSVersion() >= 170 && nif->blockInherits( iShape, "BSGeometry" ) ) {
		auto meshes = nif->getIndex( iShape, "Meshes" );
		if ( !meshes.isValid() )
			return false;

		int	sfMeshLOD = 0;
		if ( auto w = dynamic_cast< NifSkope * >( nif->getWindow() ); w ) {
			auto	ogl = w->getGLView();
			if ( ogl && ogl->getScene() )
				sfMeshLOD = ogl->getScene()->lodLevel;
		}
		int	lodDiff = 255;
		for ( int i = 0; i <= 3; i++ ) {
			auto mesh = nif->getIndex( meshes, i );
			if ( !mesh.isValid() )
				continue;
			auto hasMesh = nif->getIndex( mesh, "Has Mesh" );
			if ( !hasMesh.isValid() || nif->get<quint8>( hasMesh ) == 0 )
				continue;
			mesh = nif->getIndex( mesh, "Mesh" );
			if ( !mesh.isValid() )
				continue;
			if ( std::abs( i - sfMeshLOD ) < lodDiff ) {
				lodDiff = std::abs( i - sfMeshLOD );
				sfMeshIndex = mesh;
			}
		}
		if ( !sfMeshIndex.isValid() )
			return false;
		MeshFile	meshFile( nif, sfMeshIndex );
		if ( !( meshFile.isValid() && meshFile.coords1.size() > 0 && meshFile.triangles.size() > 0 ) )
			return false;

		if ( ( nif->get<quint32>(iShape, "Flags") & 0x0200 ) == 0 ) {
			texcoords = meshFile.coords1;
			if ( !setTexCoords( &(meshFile.triangles) ) )
				return false;

			QAction * aExportSFMesh = new QAction( tr( "Export Mesh File" ), this );
			connect( aExportSFMesh, &QAction::triggered, this, &UVWidget::exportSFMesh );
			addAction( aExportSFMesh );
			// Fake index so that isValid() checks do not fail
			iTexCoords = iShape;
		} else {
			iShapeData = nif->getIndex( sfMeshIndex, "Mesh Data" );
			iTexCoords = nif->getIndex( iShapeData, "UVs" );

			if ( !setTexCoords() )
				return false;

			if ( meshFile.coords2.size() >= meshFile.coords1.size() && !coordSetSelect ) {
				coordSetSelect = new QMenu( tr( "Select Coordinate Set" ) );
				addAction( coordSetSelect->menuAction() );
				connect( coordSetSelect, &QMenu::aboutToShow, this, &UVWidget::getCoordSets );

				coordSetGroup = new QActionGroup( this );
				connect( coordSetGroup, &QActionGroup::triggered, this, &UVWidget::selectCoordSet );
			}
		}
	}

	currentTexFile = 0;
	validTexs.clear();
	texfiles.clear();
	auto props = nif->getLinkArray( iShape, "Properties" );
	props << nif->getLink( iShape, "Shader Property" );
	for ( const auto l : props )
	{
		QModelIndex iTexProp = nif->getBlockIndex( l, "NiTexturingProperty" );

		if ( iTexProp.isValid() ) {
			for ( int currentTexSlot = 0; currentTexSlot < texnames.size(); currentTexSlot++ ) {
				iTex = nif->getIndex( iTexProp, texnames[currentTexSlot] );

				if ( !iTex.isValid() && ( currentTexSlot == 4 || currentTexSlot == 5 ) )
					iTex = nif->getIndex( iTexProp, texnames[0] );
				if ( !iTex.isValid() )
					continue;

				QModelIndex iTexSource = nif->getBlockIndex( nif->getLink( iTex, "Source" ) );

				if ( iTexSource.isValid() ) {
					currentCoordSet = nif->get<int>( iTex, "UV Set" );
					iTexCoords = nif->getIndex( nif->getIndex( iShapeData, "UV Sets" ), currentCoordSet );
					texsource  = iTexSource;

					if ( setTexCoords() )
						return true;
				}
			}
		} else {
			iTexProp = nif->getBlockIndex( l, "NiTextureProperty" );

			if ( iTexProp.isValid() ) {
				QModelIndex iTexSource = nif->getBlockIndex( nif->getLink( iTexProp, "Image" ) );

				if ( iTexSource.isValid() ) {
					texsource = iTexSource;
					return true;
				}
			} else {
				iTexProp = nif->getBlockIndex( l, "BSShaderPPLightingProperty" );

				if ( !iTexProp.isValid() ) {
					iTexProp = nif->getBlockIndex( l, "BSLightingShaderProperty" );
					if ( iTexProp.isValid() ) {
						setTexturePaths( nif, iTexProp );
						iTexProp = QModelIndex();
					}
				}

				if ( iTexProp.isValid() ) {
					QModelIndex iTexSource = nif->getBlockIndex( nif->getLink( iTexProp, "Texture Set" ) );

					if ( iTexSource.isValid() ) {
						// Assume that a FO3 mesh never has embedded textures...
#if 0
						texsource = iTexSource;
						return true;
#endif
						QModelIndex iTextures = nif->getIndex( iTexSource, "Textures" );

						if ( iTextures.isValid() ) {
							int	n = nif->rowCount( iTextures );
							for ( int i = 0; i < n; i++ ) {
								if ( i == 4 )
									continue;
								TextureInfo	t( nif, nif->get<QString>( nif->getIndex( iTextures, i ) ) );
								if ( !t.name.isEmpty() )
									texfiles.append( t );
							}
							return true;
						}
					}
				} else {
					iTexProp = nif->getBlockIndex( l, "BSEffectShaderProperty" );

					if ( iTexProp.isValid() ) {
						setTexturePaths( nif, iTexProp );
						return true;
					}
				}
			}
		}
	}

	return true;
}

bool UVWidget::setTexCoords( const QVector<Triangle> * triangles )
{
	faces.clear();
	texcoords2faces.clear();

	if ( iTexCoords.isValid() && nif->isArray( iTexCoords ) )
		texcoords = nif->getArray<Vector2>( iTexCoords );

	QVector<Triangle> tris;

	if ( triangles ) {
		tris = *triangles;
	} else if ( nif->isNiBlock( iShapeData, "NiTriShapeData" ) || nif->getBSVersion() >= 170 ) {
		tris = nif->getArray<Triangle>( iShapeData, "Triangles" );
	} else if ( nif->isNiBlock( iShapeData, "NiTriStripsData" ) ) {
		QModelIndex iPoints = nif->getIndex( iShapeData, "Points" );

		if ( !iPoints.isValid() )
			return false;

		for ( int r = 0; r < nif->rowCount( iPoints ); r++ ) {
			tris += triangulate( nif->getArray<quint16>( nif->getIndex( iPoints, r ) ) );
		}
	} else if ( nif->blockInherits( iShape, "BSTriShape" ) ) {
		if ( !isDataOnSkin ) {
			tris = nif->getArray<Triangle>( iShape, "Triangles" );
		} else {
			auto partIdx = nif->getIndex( iPartBlock, "Partitions" );
			for ( int i = 0; i < nif->rowCount( partIdx ); i++ ) {
				tris << nif->getArray<Triangle>( nif->index( i, 0, partIdx ), "Triangles" );
			}
		}
	}

	if ( tris.isEmpty() || texcoords.isEmpty() )
		return false;

	QVectorIterator<Triangle> itri( tris );

	unsigned int	numVerts = (unsigned int) texcoords.size();
	while ( itri.hasNext() ) {
		Triangle	t = itri.next();
		unsigned int	d = std::min( t[0], std::min( t[1], t[2] ) );
		if ( d >= numVerts )
			d = 0;
		int	fIdx = int( faces.size() );
		for ( int i = 0; i < 3; i++ ) {
			if ( t[i] >= numVerts )
				t[i] = d;		// remove invalid indices
			else
				texcoords2faces.insert( int( t[i] ), fIdx );
		}
		faces.append( t );
	}

	return true;
}

void UVWidget::updateNif()
{
	if ( nif && iTexCoords.isValid() ) {
		disconnect( nif, &NifModel::dataChanged, this, &UVWidget::nifDataChanged );
		nif->setState( BaseModel::Processing );

		if ( sfMeshIndex.isValid() && nif->isArray( iTexCoords ) ) {
			int	numVerts = int( texcoords.size() );
			NifItem *	texcoordsItem = nif->getItem( iTexCoords );
			if ( !texcoordsItem )
				numVerts = 0;
			else
				numVerts = std::min< int >( numVerts, texcoordsItem->childCount() );
			for ( int i = 0; i < numVerts; i++ ) {
				NifItem *	j = texcoordsItem->child( i );
				if ( j )
					nif->set<HalfVector2>( j, texcoords.at( i ) );
			}
			nif->dataChanged( iTexCoords, iTexCoords );
		} else if ( nif->blockInherits( iShapeData, "NiTriBasedGeomData" ) ) {
			nif->setArray<Vector2>( iTexCoords, texcoords );
		} else if ( nif->blockInherits( iShape, "BSTriShape" ) ) {
			int numVerts = 0;
			if ( !isDataOnSkin )
				numVerts = nif->get<int>( iShape, "Num Vertices" );
			else
				numVerts = nif->get<uint>( iPartBlock, "Data Size" ) / nif->get<uint>( iPartBlock, "Vertex Size" );

			for ( int i = 0; i < numVerts; i++ ) {
				nif->set<HalfVector2>( nif->index( i, 0, iShapeData ), "UV", HalfVector2( texcoords.value( i ) ) );
			}

			nif->dataChanged( iShape, iShape );
		}

		nif->restoreState();
		connect( nif, &NifModel::dataChanged, this, &UVWidget::nifDataChanged );
	}
}

void UVWidget::nifDataChanged( const QModelIndex & idx )
{
	if ( !nif || !iShape.isValid() || !iShapeData.isValid() || !iTexCoords.isValid() ) {
		close();
		return;
	}

	if ( nif->getBlockIndex( idx ) == iShapeData ) {
		//if ( ! setNifData( nif, iShape ) )
		{
			close();
			return;
		}
	}
}

bool UVWidget::isSelected( int index )
{
	return selection.contains( index );
}

class UVWSelectCommand final : public QUndoCommand
{
public:
	UVWSelectCommand( UVWidget * w, const QList<int> & nSel ) : QUndoCommand(), uvw( w ), newSelection( nSel )
	{
		setText( "Select" );
	}

	int id() const override final
	{
		return 0;
	}

	bool mergeWith( const QUndoCommand * cmd ) override final
	{
		if ( cmd->id() == id() ) {
			newSelection = static_cast<const UVWSelectCommand *>( cmd )->newSelection;
			return true;
		}

		return false;
	}

	void redo() override final
	{
		oldSelection = uvw->selection;
		uvw->selection = newSelection;
		uvw->update();
	}

	void undo() override final
	{
		uvw->selection = oldSelection;
		uvw->update();
	}

protected:
	UVWidget * uvw;
	QList<int> oldSelection, newSelection;
};

void UVWidget::select( int index, bool yes )
{
	QList<int> sel = this->selection;

	if ( yes ) {
		if ( !sel.contains( index ) )
			sel.append( index );
	} else {
		sel.removeAll( index );
	}

	undoStack->push( new UVWSelectCommand( this, sel ) );
}

void UVWidget::select( const QRegion & r, bool add )
{
	QList<int> sel( add ? this->selection : QList<int>() );
	for ( const auto s : indices( r ) ) {
		if ( !sel.contains( s ) )
			sel.append( s );
	}
	undoStack->push( new UVWSelectCommand( this, sel ) );
}

void UVWidget::selectNone()
{
	undoStack->push( new UVWSelectCommand( this, QList<int>() ) );
}

void UVWidget::selectAll()
{
	QList<int> sel;

	for ( int s = 0; s < texcoords.count(); s++ )
		sel << s;

	undoStack->push( new UVWSelectCommand( this, sel ) );
}

void UVWidget::selectFaces()
{
	QList<int> sel = this->selection;
	for ( const auto s : QList<int>( sel ) ) {
		for ( const auto f : texcoords2faces.values( s ) ) {
			for ( int i = 0; i < 3; i++ ) {
				if ( !sel.contains( faces.at(f)[i] ) )
					sel.append( faces.at(f)[i] );
			}
		}
	}
	undoStack->push( new UVWSelectCommand( this, sel ) );
}

void UVWidget::selectConnected()
{
	QList<int> sel = this->selection;
	bool more = true;

	while ( more ) {
		more = false;
		for ( const auto s : QList<int>( sel ) ) {
			for ( const auto f :texcoords2faces.values( s ) ) {
				for ( int i = 0; i < 3; i++ ) {
					if ( !sel.contains( faces.at(f)[i] ) ) {
						sel.append( faces.at(f)[i] );
						more = true;
					}
				}
			}
		}
	}

	undoStack->push( new UVWSelectCommand( this, sel ) );
}

class UVWMoveCommand final : public QUndoCommand
{
public:
	UVWMoveCommand( UVWidget * w, double dx, double dy ) : QUndoCommand(), uvw( w ), move( dx, dy )
	{
		setText( "Move" );
	}

	int id() const override final
	{
		return 1;
	}

	bool mergeWith( const QUndoCommand * cmd ) override final
	{
		if ( cmd->id() == id() ) {
			move += static_cast<const UVWMoveCommand *>( cmd )->move;
			return true;
		}

		return false;
	}

	void redo() override final
	{
		for ( const auto tc : uvw->selection ) {
			uvw->texcoords[tc] += move;
		}
		uvw->updateNif();
		uvw->update();
	}

	void undo() override final
	{
		for ( const auto tc : uvw->selection ) {
			uvw->texcoords[tc] -= move;
		}
		uvw->updateNif();
		uvw->update();
	}

protected:
	UVWidget * uvw;
	Vector2 move;
};

void UVWidget::moveSelection( double moveX, double moveY )
{
	undoStack->push( new UVWMoveCommand( this, moveX, moveY ) );
}

// For mouse-driven scaling: insert state/flag for scaling mode
// get difference in mouse coords, scale everything around centre

//! A class to perform scaling of UV coordinates
class UVWScaleCommand final : public QUndoCommand
{
public:
	UVWScaleCommand( UVWidget * w, float sX, float sY ) : QUndoCommand(), uvw( w ), scaleX( sX ), scaleY( sY )
	{
		setText( "Scale" );
	}

	int id() const override final
	{
		return 2;
	}

	bool mergeWith( const QUndoCommand * cmd ) override final
	{
		if ( cmd->id() == id() ) {
			scaleX *= static_cast<const UVWScaleCommand *>( cmd )->scaleX;
			scaleY *= static_cast<const UVWScaleCommand *>( cmd )->scaleY;
			return true;
		}

		return false;
	}

	void redo() override final
	{
		Vector2 centre;
		for ( const auto i : uvw->selection ) {
			centre += uvw->texcoords[i];
		}
		centre /= uvw->selection.size();

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] -= centre;
		}

		for ( const auto i : uvw->selection ) {
			Vector2 temp( uvw->texcoords[i] );
			uvw->texcoords[i] = Vector2( temp[0] * scaleX, temp[1] * scaleY );
		}

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] += centre;
		}

		uvw->updateNif();
		uvw->update();
	}

	void undo() override final
	{
		Vector2 centre;
		for ( const auto i : uvw->selection ) {
			centre += uvw->texcoords[i];
		}
		centre /= uvw->selection.size();

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] -= centre;
		}

		for ( const auto i : uvw->selection ) {
			Vector2 temp( uvw->texcoords[i] );
			uvw->texcoords[i] = Vector2( temp[0] / scaleX, temp[1] / scaleY );
		}

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] += centre;
		}

		uvw->updateNif();
		uvw->update();
	}

protected:
	UVWidget * uvw;
	float scaleX, scaleY;
};

void UVWidget::scaleSelection()
{
	ScalingDialog * scaleDialog = new ScalingDialog( this );

	if ( scaleDialog->exec() == QDialog::Accepted ) {
		// order does not matter here, since we scale around the center
		// don't perform identity transforms
		if ( !( scaleDialog->getXScale() == 1.0 && scaleDialog->getYScale() == 1.0 ) ) {
			undoStack->push( new UVWScaleCommand( this, scaleDialog->getXScale(), scaleDialog->getYScale() ) );
		}

		if ( !( scaleDialog->getXMove() == 0.0 && scaleDialog->getYMove() == 0.0 ) ) {
			undoStack->push( new UVWMoveCommand( this, scaleDialog->getXMove(), scaleDialog->getYMove() ) );
		}
	}
}

ScalingDialog::ScalingDialog( QWidget * parent ) : QDialog( parent )
{
	grid = new QGridLayout;
	setLayout( grid );
	int currentRow = 0;

	grid->addWidget( new QLabel( tr( "Enter scaling factors" ) ), currentRow, 0, 1, -1 );
	currentRow++;

	grid->addWidget( new QLabel( "X: " ), currentRow, 0, 1, 1 );
	spinXScale = new QDoubleSpinBox;
	spinXScale->setValue( 1.0 );
	spinXScale->setRange( -MAXSCALE, MAXSCALE );
	grid->addWidget( spinXScale, currentRow, 1, 1, 1 );

	grid->addWidget( new QLabel( "Y: " ), currentRow, 2, 1, 1 );
	spinYScale = new QDoubleSpinBox;
	spinYScale->setValue( 1.0 );
	spinYScale->setRange( -MAXSCALE, MAXSCALE );
	grid->addWidget( spinYScale, currentRow, 3, 1, 1 );
	currentRow++;

	uniform = new QCheckBox;
	connect( uniform, &QCheckBox::toggled, this, &ScalingDialog::setUniform );
	uniform->setChecked( true );
	grid->addWidget( uniform, currentRow, 0, 1, 1 );
	grid->addWidget( new QLabel( tr( "Uniform scaling" ) ), currentRow, 1, 1, -1 );
	currentRow++;

	grid->addWidget( new QLabel( tr( "Enter translation amounts" ) ), currentRow, 0, 1, -1 );
	currentRow++;

	grid->addWidget( new QLabel( "X: " ), currentRow, 0, 1, 1 );
	spinXMove = new QDoubleSpinBox;
	spinXMove->setValue( 0.0 );
	spinXMove->setRange( -MAXTRANS, MAXTRANS );
	grid->addWidget( spinXMove, currentRow, 1, 1, 1 );

	grid->addWidget( new QLabel( "Y: " ), currentRow, 2, 1, 1 );
	spinYMove = new QDoubleSpinBox;
	spinYMove->setValue( 0.0 );
	spinYMove->setRange( -MAXTRANS, MAXTRANS );
	grid->addWidget( spinYMove, currentRow, 3, 1, 1 );
	currentRow++;

	QPushButton * ok = new QPushButton( tr( "OK" ) );
	grid->addWidget( ok, currentRow, 0, 1, 2 );
	connect( ok, &QPushButton::clicked, this, &ScalingDialog::accept );

	QPushButton * cancel = new QPushButton( tr( "Cancel" ) );
	grid->addWidget( cancel, currentRow, 2, 1, 2 );
	connect( cancel, &QPushButton::clicked, this, &ScalingDialog::reject );
}

float ScalingDialog::getXScale()
{
	return spinXScale->value();
}

float ScalingDialog::getYScale()
{
	return spinYScale->value();
}

void ScalingDialog::setUniform( bool status )
{
	// Cast QDoubleSpinBox slot
	auto dsbValueChanged = static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged);

	if ( status == true ) {
		connect( spinXScale, dsbValueChanged, spinYScale, &QDoubleSpinBox::setValue );
		spinYScale->setEnabled( false );
		spinYScale->setValue( spinXScale->value() );
	} else {
		disconnect( spinXScale, dsbValueChanged, spinYScale, &QDoubleSpinBox::setValue );
		spinYScale->setEnabled( true );
	}
}

float ScalingDialog::getXMove()
{
	return spinXMove->value();
}

float ScalingDialog::getYMove()
{
	return spinYMove->value();
}

//! A class to perform rotation of UV coordinates
class UVWRotateCommand final : public QUndoCommand
{
public:
	UVWRotateCommand( UVWidget * w, float r ) : QUndoCommand(), uvw( w ), rotation( r )
	{
		setText( "Rotation" );
	}

	int id() const override final
	{
		return 3;
	}

	bool mergeWith( const QUndoCommand * cmd ) override final
	{
		if ( cmd->id() == id() ) {
			rotation += static_cast<const UVWRotateCommand *>( cmd )->rotation;
			rotation -= 360.0 * (int)( rotation / 360.0 );
			return true;
		}

		return false;
	}

	void redo() override final
	{
		Vector2 centre;
		for ( const auto i : uvw->selection ) {
			centre += uvw->texcoords[i];
		}
		centre /= uvw->selection.size();

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] -= centre;
		}

		Matrix rotMatrix;
		rotMatrix.fromEuler( 0, 0, deg2rad(rotation) );

		for ( const auto i : uvw->selection ) {
			Vector3 temp( uvw->texcoords[i], 0 );
			temp = rotMatrix * temp;
			uvw->texcoords[i] = Vector2( temp[0], temp[1] );
		}

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] += centre;
		}

		uvw->updateNif();
		uvw->update();
	}

	void undo() override final
	{
		Vector2 centre;
		for ( const auto i : uvw->selection ) {
			centre += uvw->texcoords[i];
		}
		centre /= uvw->selection.size();

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] -= centre;
		}

		Matrix rotMatrix;
		rotMatrix.fromEuler( 0, 0, -deg2rad(rotation) );

		for ( const auto i : uvw->selection ) {
			Vector3 temp( uvw->texcoords[i], 0 );
			temp = rotMatrix * temp;
			uvw->texcoords[i] = Vector2( temp[0], temp[1] );
		}

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] += centre;
		}

		uvw->updateNif();
		uvw->update();
	}

protected:
	UVWidget * uvw;
	float rotation;
};

void UVWidget::rotateSelection()
{
	bool ok;
	float rotateFactor = QInputDialog::getDouble( this, "NifSkope", tr( "Enter rotation angle" ), 0.0, -360.0, 360.0, 2, &ok );

	if ( ok ) {
		undoStack->push( new UVWRotateCommand( this, rotateFactor ) );
	}
}

void UVWidget::rotate_Selection( float r )
{
	undoStack->push( new UVWRotateCommand( this, r ) );
}

void UVWidget::exportSFMesh()
{
	if ( !nif || nif->getBSVersion() < 170 || !sfMeshIndex.isValid() )
		return;

	QModelIndex	iMeshPath = nif->getIndex( sfMeshIndex, "Mesh Path" );
	if ( !iMeshPath.isValid() )
		return;
	QString	sfMeshPath = nif->findResourceFile( nif->get<QString>( iMeshPath ), "geometries/", ".mesh" );
	if ( sfMeshPath.isEmpty() )
		return;

	QByteArray	sfMeshData;
	if ( !nif->getResourceFile( sfMeshData, sfMeshPath, nullptr, nullptr ) )
		return;
	unsigned char *	meshData = reinterpret_cast< unsigned char * >( sfMeshData.data() );
	size_t	meshDataSize = size_t( sfMeshData.size() );
	size_t	numTexCoords;
	unsigned char *	uvData;

	// find position of UV data in the file
	try {
		FileBuffer	meshBuf( meshData, meshDataSize );
		if ( ( meshBuf.readUInt32() - 1U ) & ~1U )
			return;	// format version must be 1 or 2
		size_t	numIndices = meshBuf.readUInt32();
		meshBuf.setPosition( meshBuf.getPosition() + ( numIndices * 2 ) );
		(void) meshBuf.readUInt64();	// skip vertex coordinate scale and number of weights per vertex
		size_t	numVertices = meshBuf.readUInt32();
		meshBuf.setPosition( meshBuf.getPosition() + ( numVertices * 6 ) );
		numTexCoords = meshBuf.readUInt32();
		if ( qsizetype(numTexCoords) != texcoords.size() ) {
			QMessageBox::critical( this, "NifSkope error", tr( "Vertex count does not match .mesh file" ) );
			return;
		}
		if ( ( meshBuf.getPosition() + ( numTexCoords * std::uint64_t(4) ) ) > meshBuf.size() )
			return;
		uvData = const_cast< unsigned char * >( meshBuf.getReadPtr() );
	} catch ( NifSkopeError & ) {
		return;
	}

	// store new UV data
	for ( size_t i = 0; i < numTexCoords; i++ ) {
		const Vector2 &	v = texcoords.at( i );
		std::uint32_t	tmp = std::uint32_t( FloatVector4( v[0], v[1], 0.0f, 0.0f ).convertToFloat16() );
		FileBuffer::writeUInt32Fast( uvData + ( i * 4 ), tmp );
	}

	// select and write output file
	QString	meshPath( QFileDialog::getSaveFileName( this, tr( "Select Mesh File" ), sfMeshPath, QString( "Mesh Files (*.mesh)" ) ) );
	if ( meshPath.isEmpty() )
		return;
	QFile	outFile( meshPath );
	if ( !outFile.open( QIODevice::WriteOnly ) ) {
		QMessageBox::critical( this, "NifSkope error", tr( "Error opening .mesh file" ) );
		return;
	}
	outFile.write( sfMeshData.data(), sfMeshData.size() );
}

void UVWidget::copyTexCoord()
{
	std::string buf;

	if ( !selection.isEmpty() ) {
		for ( auto i : selection ) {
			if ( i >= 0 && i < texcoords.size() ) {
				const auto & v = texcoords.at( i );
				printToString( buf, "vt %.8g %.8g %d\n", v[0], 1.0 - v[1], i + 1 );
			}
		}
	} else {
		for ( const auto & v : texcoords )
			printToString( buf, "vt %.8g %.8g\n", v[0], 1.0 - v[1] );
	}

	if ( QClipboard * clipboard = QGuiApplication::clipboard(); clipboard && !buf.empty() )
		clipboard->setText( QString::fromStdString( buf ) );
}

void UVWidget::pasteTexCoord()
{
	std::map< int, Vector2 > clipboardData;

	if ( QClipboard * clipboard = QGuiApplication::clipboard(); clipboard ) {
		if ( auto clipboardText = clipboard->text(); !clipboardText.isEmpty() ) {
			QTextStream s( &clipboardText, QIODevice::ReadOnly );
			QString l;
			int i = 0;
			while ( !( l = s.readLine() ).isNull() ) {
				if ( auto v = l.split( QChar(' '), Qt::SkipEmptyParts ); v.size() >= 3 && v.at( 0 ) == "vt" ) {
					i++;
					bool isValid = false;
					float u = v.at( 1 ).toFloat( &isValid );
					if ( !isValid )
						continue;
					if ( v.size() >= 4 )
						i = v.at( 3 ).toInt();
					if ( i > 0 )
						clipboardData[i - 1] = Vector2( u, float( 1.0 - v.at( 2 ).toDouble() ) );
				}
			}
		}
	}

	if ( clipboardData.empty() )
		return;
	if ( QMessageBox::question( this, tr( "NifSkope warning" ),
								tr( "This action cannot currently be undone. Do you want to continue?" ) )
			!= QMessageBox::Yes ) {
		return;
	}

	int vertexOffs = 0;
	if ( clipboardData.size() < size_t( texcoords.size() ) && !selection.isEmpty() ) {
		int selectionBase = selection.at( 0 );
		for ( int i : selection )
			selectionBase = std::min( selectionBase, i );
		vertexOffs = selectionBase - clipboardData.cbegin()->first;
	}

	for ( const auto & v : clipboardData ) {
		int i = v.first + vertexOffs;
		if ( i >= 0 && i < texcoords.size() )
			texcoords[i] = v.second;
	}

	// TODO: implement undo support
	updateNif();
	update();
}

void UVWidget::getTexSlots()
{
	if ( texfiles.isEmpty() && validTexs.isEmpty() && nif->getBSVersion() < 83 ) {
		auto props = nif->getLinkArray( iShape, "Properties" );
		props << nif->getLink( iShape, "Shader Property" );
		for ( const auto l : props )
		{
			QModelIndex iTexProp = nif->getBlockIndex( l, "NiTexturingProperty" );
			if ( !iTexProp.isValid() )
				continue;

			int	n = 0;
			for ( const QString & name : texnames ) {
				QString	texturePath;
				if ( nif->get<bool>( iTexProp, QString( "Has %1" ).arg( name ) ) ) {
					QModelIndex	i = nif->getIndex( iTexProp, name );
					if ( i.isValid() ) {
						i = nif->getBlockIndex( nif->getLink( i, "Source" ) );
						if ( i.isValid() ) {
							i = nif->getIndex( i, "File Name" );
							if ( i.isValid() )
								texturePath = nif->get<QString>( i );
						}
					}
				} else if ( n == 4 || n == 5 ) {
					texturePath = getTES4NormalOrGlowMap( nif, iTexProp, n );
				}
				if ( !texturePath.isEmpty() ) {
					TextureInfo	t( nif, texturePath );
					if ( !t.name.isEmpty() ) {
						texfiles.append( t );
						validTexs.append( n );
					}
				}
				n++;
			}
		}
	}

	if ( !texSlotGroup->actions().isEmpty() )
		return;
	menuTexSelect->clear();
	int	i = 0;
	for ( const auto & t : texfiles ) {
		const QString &	name = ( i < validTexs.size() ? texnames[validTexs[i]] : t.name );
		QAction * temp = new QAction( name, this );
		menuTexSelect->addAction( temp );
		texSlotGroup->addAction( temp );
		temp->setCheckable( true );
		if ( i == currentTexFile )
			temp->setChecked( true );
		i++;
	}
}

void UVWidget::selectTexSlot()
{
	auto	a = texSlotGroup->actions();
	auto	selected = texSlotGroup->checkedAction();

	currentTexFile = -1;
	for ( qsizetype i = 0; i < a.size(); i++ ) {
		if ( a[i] == selected ) {
			currentTexFile = int( i );
			break;
		}
	}
	if ( currentTexFile < 0 || currentTexFile >= texfiles.size() )
		currentTexFile = 0;
	if ( nif->getBSVersion() >= 83 )
		return;

	int	currentTexSlot = 0;
	if ( currentTexFile >= 0 && currentTexFile < validTexs.size() )
		currentTexSlot = validTexs[currentTexFile];

	auto props = nif->getLinkArray( iShape, "Properties" );
	props << nif->getLink( iShape, "Shader Property" );
	for ( const auto l : props )
	{
		QModelIndex iTexProp = nif->getBlockIndex( l, "NiTexturingProperty" );

		if ( iTexProp.isValid() ) {
			iTex = nif->getIndex( iTexProp, texnames[currentTexSlot] );

			if ( !iTex.isValid() && ( currentTexSlot == 4 || currentTexSlot == 5 ) )
				iTex = nif->getIndex( iTexProp, texnames[0] );

			if ( iTex.isValid() ) {
				QModelIndex iTexSource = nif->getBlockIndex( nif->getLink( iTex, "Source" ) );

				if ( iTexSource.isValid() ) {
					currentCoordSet = nif->get<int>( iTex, "UV Set" );
					iTexCoords = nif->getIndex( nif->getIndex( iShapeData, "UV Sets" ), currentCoordSet );
					texsource  = iTexSource;
					setTexCoords();
					update();
					return;
				}
			}
		}
	}
}

void UVWidget::getCoordSets()
{
	coordSetSelect->clear();

	quint8	numUvSets = 0;
	int	uvSetOffs = 0;

	if ( nif->getBSVersion() >= 170 ) {
		quint32	numVerts = nif->get<quint32>( iShapeData, "Num Verts" );
		if ( nif->get<quint32>( iShapeData, "Num UVs" ) >= numVerts )
			numUvSets++;
		if ( nif->get<quint32>( iShapeData, "Num UVs 2" ) >= numVerts )
			numUvSets++;
		uvSetOffs = 1;
	} else {
		numUvSets = ( nif->get<quint16>( iShapeData, "Data Flags" ) & 0x3F )
					| ( nif->get<quint16>( iShapeData, "BS Data Flags" ) & 0x1 );
	}

	for ( int i = 0; i < numUvSets; i++ ) {
		QAction * temp = new QAction( QString::number( i + uvSetOffs ), this );
		coordSetSelect->addAction( temp );
		coordSetGroup->addAction( temp );
		temp->setCheckable( true );

		if ( i == currentCoordSet ) {
			temp->setChecked( true );
		}
	}

	if ( !uvSetOffs ) {
		// TODO: implement this for Starfield
		coordSetSelect->addSeparator();
		aDuplicateCoords = new QAction( tr( "Duplicate current" ), this );
		coordSetSelect->addAction( aDuplicateCoords );
		connect( aDuplicateCoords, &QAction::triggered, this, &UVWidget::duplicateCoordSet );
	}
}

void UVWidget::selectCoordSet()
{
	auto	a = coordSetSelect->actions();
	auto	selected = coordSetGroup->checkedAction();

	int	setToUse = -1;
	for ( qsizetype i = 0; i < a.size(); i++ ) {
		if ( a[i] == selected ) {
			setToUse = int( i );
			break;
		}
	}
	if ( setToUse < 0 )
		return;

	// write all changes
	updateNif();
	// change coordinate set
	changeCoordSet( setToUse );
}

void UVWidget::changeCoordSet( int setToUse )
{
	currentCoordSet = setToUse;
	if ( nif->getBSVersion() < 170 ) {
		// update
		nif->set<quint8>( iTex, "UV Set", currentCoordSet );
		// read new coordinate set
		iTexCoords = nif->getIndex( nif->getIndex( iShapeData, "UV Sets" ), currentCoordSet );
	} else {
		iTexCoords = nif->getIndex( iShapeData, ( !setToUse ? "UVs" : "UVs 2" ) );
	}
	setTexCoords();
}


void UVWidget::duplicateCoordSet()
{
	// this signal close the UVWidget
	disconnect( nif, &NifModel::dataChanged, this, &UVWidget::nifDataChanged );
	// expand the UV Sets array and duplicate the current coordinates
	auto dataFlags = nif->get<quint16>( iShapeData, "Data Flags" );
	quint8 numUvSets = nif->get<quint16>( iShapeData, "Data Flags" ) & 0x3F;
	numUvSets += 1;
	dataFlags = dataFlags | ((dataFlags & 0x3F) | numUvSets);

	nif->set<quint8>( iShapeData, "Data Flags", numUvSets );
	QModelIndex uvSets = nif->getIndex( iShapeData, "UV Sets" );
	nif->updateArraySize( uvSets );
	nif->setArray<Vector2>( nif->getIndex( uvSets, numUvSets ), nif->getArray<Vector2>( nif->getIndex( uvSets, currentCoordSet ) ) );
	// switch to that coordinate set
	changeCoordSet( numUvSets );
	// reconnect data changed signal
	connect( nif, &NifModel::dataChanged, this, &UVWidget::nifDataChanged );
}

