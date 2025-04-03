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

#include "glview.h"

#include "message.h"
#include "nifskope.h"
#include "gl/renderer.h"
#include "gl/glshape.h"
#include "gl/gltex.h"
#include "model/nifmodel.h"
#include "ui/settingsdialog.h"
#include "ui/widgets/fileselect.h"
#include "fp32vec4.hpp"
#include "ui/widgets/filebrowser.h"

#include <QApplication>
#include <QActionGroup>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QGroupBox>
#include <QImageWriter>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QSurface>
#include <QTimer>
#include <QToolBar>
#include <QWindow>

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>

// NOTE: The FPS define is a frame limiter,
//	NOT the guaranteed FPS in the viewport.
//	Also the QTimer is integer milliseconds
//	so 60 will give you 1000/60 = 16, not 16.666
//	therefore it's really 62.5FPS
#define FPS 144

#define ZOOM_MIN 1.0
#define ZOOM_MAX 1000.0

#define DEBUG_FRAME_TIME 0

//! @file glview.cpp GLView implementation


const Vector3 GLView::viewRotations[6] = {
	{ 0.0f, 0.0f, 0.0f },		// Top
	{ 180.0f, 0.0f, 0.0f },		// Bottom
	{ -90.0f, 0.0f, -90.0f },	// Left
	{ -90.0f, 0.0f, 90.0f },	// Right
	{ -90.0f, 0.0f, 180.0f },	// Front
	{ -90.0f, 0.0f, 0.0f }		// Back
};

GLView::GLView( QWindow * p )
	: QOpenGLWindow( QOpenGLWindow::NoPartialUpdate, p )
{
	QSettings settings;
	int	aa = settings.value( "Settings/Render/General/Msaa Samples", 2 ).toInt();
	aa = std::clamp< int >( aa, 0, 4 );

	QSurfaceFormat	fmt;

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
	fmt.setSwapInterval( DEBUG_FRAME_TIME ? 0 : 1 );
	fmt.setSwapBehavior( QSurfaceFormat::DoubleBuffer );

	fmt.setDepthBufferSize( 24 );
	fmt.setStencilBufferSize( 8 );
	fmt.setSamples( 1 << aa );

	setFormat( fmt );

	view = ViewDefault;
	debugMode = DbgNone;
	perspectiveMode = true;
	contextMenuShiftModifier = false;
	animState = AnimEnabled;

	Zoom = 1.0;

	doCenter  = false;
	doCompile = 0;

	model = nullptr;

	time = 0.0f;
	Dist = 128.0f;
	lastTime = std::chrono::steady_clock::now();

	textures = new TexCache( this );

	updateSettings();
	view = cfg.startupDirection;
	if ( int i = int( view ) - int( ViewTop ); i >= 0 && i <= 5 )
		Rot = viewRotations[i];

	scene = new Scene( textures );
	connect( textures, &TexCache::sigRefresh, this, static_cast<void (GLView::*)()>(&GLView::update) );
	connect( scene, &Scene::sceneUpdated, this, static_cast<void (GLView::*)()>(&GLView::update) );

	timer = new QTimer( this );
	timer->setInterval( 1000 / FPS );
	timer->start();
	connect( timer, &QTimer::timeout, this, &GLView::advanceGears );

	lightVisTimeout = 1500;
	lightVisTimer = new QTimer( this );
	lightVisTimer->setSingleShot( true );
	connect( lightVisTimer, &QTimer::timeout, [this]() { setVisMode( Scene::VisLightPos, false ); update(); } );

	connect( NifSkope::getOptions(), &SettingsDialog::flush3D, textures, &TexCache::flush );
	connect( NifSkope::getOptions(), &SettingsDialog::update3D, this, &GLView::update3D );

	setMinimumSize( QSize( 50, 50 ) );
}

GLView::~GLView()
{
	auto	prvContext = pushGLContext();

	flush();
	delete textures;
	delete scene;

	popGLContext( prvContext );
}

QWidget * GLView::createWindowContainer( QWidget * parent )
{
	graphicsView = QWidget::createWindowContainer( this, parent );
	graphicsView->setContextMenuPolicy( Qt::PreventContextMenu );
	graphicsView->setFocusPolicy( Qt::ClickFocus );
	graphicsView->setAcceptDrops( true );
	graphicsView->setMinimumSize( QSize( 50, 50 ) );

	graphicsView->installEventFilter( parent );
	installEventFilter( graphicsView );

	return graphicsView;
}

float	GLView::Settings::vertexPointSize = 5.0f;
float	GLView::Settings::tbnPointSize = 7.0f;
float	GLView::Settings::vertexSelectPointSize = 8.5f;
float	GLView::Settings::vertexPointSizeSelected = 10.0f;
float	GLView::Settings::lineWidthAxes = 2.0f;
float	GLView::Settings::lineWidthWireframe = 1.6f;
float	GLView::Settings::lineWidthHighlight = 2.5f;
float	GLView::Settings::lineWidthGrid = 1.4f;
float	GLView::Settings::lineWidthSelect = 5.0f;
float	GLView::Settings::zoomInScale = 0.95f;
float	GLView::Settings::zoomOutScale = 1.0f / 0.95f;

void GLView::updateSettings()
{
	QSettings settings;
	settings.beginGroup( "Settings/Render" );

	cfg.background = Color4( settings.value( "Colors/Background", QColor( 46, 46, 46 ) ).value<QColor>() );
	cfg.fov = settings.value( "General/Camera/Field Of View" ).toFloat();
	cfg.moveSpd = settings.value( "General/Camera/Movement Speed" ).toFloat();
	cfg.rotSpd = settings.value( "General/Camera/Rotation Speed" ).toFloat();
	cfg.upAxis = UpAxis(settings.value( "General/Up Axis", ZAxis ).toInt());
	int	z = settings.value( "General/Camera/Startup Direction", 1 ).toInt();
	static const ViewState	startupDirections[6] = {
		ViewLeft, ViewFront, ViewTop, ViewRight, ViewBack, ViewBottom
	};
	cfg.startupDirection = startupDirections[std::clamp< int >( z, 0, 5 )];
	z = settings.value( "General/Camera/Mwheel Zoom Speed", 8 ).toInt();
	z = std::clamp< int >( z, 0, 16 );

	settings.endGroup();

	if ( scene )
		scene->updateSettings( settings );

	// TODO: make these configurable via the UI
	double	p = devicePixelRatioF();
	Settings::vertexPointSize = float( p * 5.0 );
	Settings::tbnPointSize = float( p * 7.0 );
	Settings::vertexSelectPointSize = float( p * 8.5 );
	Settings::vertexPointSizeSelected = float( p * 10.0 );
	Settings::lineWidthAxes = float( p * 2.0 );
	Settings::lineWidthWireframe = float( p * 1.6 );
	Settings::lineWidthHighlight = float( p * 2.5 );
	Settings::lineWidthGrid = float( p * 1.4 );
	Settings::lineWidthSelect = float( p * 5.0 );

	double	tmp = std::pow( 0.95, std::sqrt( double(1 << z) * (1.0 / 256.0) ) );
	Settings::zoomInScale = float( tmp );
	Settings::zoomOutScale = float( 1.0 / tmp );
}

void GLView::update3D()
{
	updateSettings();
	auto	prvContext = pushGLContext();
	glClearColor( cfg.background.red(), cfg.background.green(), cfg.background.blue(), cfg.background.alpha() );
	popGLContext( prvContext );
	update();
}

static bool envMapFileListFilterFunction( void * p, const std::string_view & s )
{
	(void) p;
	if ( !s.starts_with("textures/") )
		return false;
	if ( !(s.ends_with(".dds") || s.ends_with(".hdr")) )
		return false;
	return ( s.find("/cubemaps/") != std::string_view::npos );
}

bool GLView::selectPBRCubeMapForGame( quint32 bsVersion )
{
	if ( bsVersion < 151 )
		return false;
	bool	isStarfield = ( bsVersion >= 170 );
	Game::GameMode	game = ( !isStarfield ? Game::FALLOUT_76 : Game::STARFIELD );
	QString	cfgPath( !isStarfield ? "Settings/Render/General/Cube Map Path FO 76" : "Settings/Render/General/Cube Map Path STF" );

	std::set< std::string_view >	fileSet;
	Game::GameManager::list_files( fileSet, game, &envMapFileListFilterFunction );
	QSettings	settings;
	std::string	prvPath( settings.value( cfgPath ).toString().toStdString() );
	if ( !prvPath.empty() && fileSet.find( prvPath ) == fileSet.end() )
		prvPath.clear();

	FileBrowserWidget	fileBrowser( 640, 480, "Select Default Environment Map", fileSet, prvPath,
										&( Game::GameManager::getGameResources( game ) ) );
	const std::string_view *	newPath = nullptr;
	if ( fileBrowser.exec() == QDialog::Accepted )
		newPath = fileBrowser.getItemSelected();
	if ( !newPath || newPath->empty() )
		return false;

	if ( NifSkope::getOptions() )
		NifSkope::getOptions()->apply();
	settings.setValue( cfgPath, QString::fromLatin1( newPath->data(), qsizetype(newPath->length()) ) );
	if ( NifSkope::getOptions() )
		emit NifSkope::getOptions()->loadSettings();

	return true;
}

void GLView::selectPBRCubeMap()
{
	if ( model && selectPBRCubeMapForGame( model->getBSVersion() ) ) {
		if ( scene && scene->renderer ) {
			scene->renderer->updateSettings();
			updateScene();
		}
	}
}

Color4 GLView::clearColor() const
{
	return cfg.background;
}


/*
 * Scene
 */

Scene * GLView::getScene()
{
	return scene;
}

void GLView::updateScene()
{
	scene->update( model, QModelIndex() );
	update();
}

void GLView::updateAnimationState( bool checked )
{
	QAction * action = qobject_cast<QAction *>(sender());
	if ( action ) {
		auto opt = AnimationState( action->data().toInt() );

		if ( checked )
			animState |= opt;
		else
			animState &= ~opt;

		scene->animate = (animState & AnimEnabled);
		lastTime = std::chrono::steady_clock::now();

		update();
	}
}


/*
 *  OpenGL
 */

void GLView::initializeGL()
{
	auto	cx = context();
	// Obtain a functions object and resolve all entry points
	auto	glFuncs = cx->functions();
	if ( !glFuncs ) {
		QMessageBox::critical( nullptr, "NifSkope error", tr( "Could not obtain OpenGL functions" ) );
		std::exit( 1 );
	}
	glFuncs->initializeOpenGLFunctions();
	scene->setOpenGLContext( cx );
	glContext = scene->renderer;
	textures->setOpenGLContext( glContext );
	updateShaders();		// should be called after TexCache is initialized
	glClearColor( cfg.background.red(), cfg.background.green(), cfg.background.blue(), cfg.background.alpha() );

	// Initial viewport values
	//	Made viewport and aspect member variables.
	//	They were being updated every single frame instead of only when resizing.
	//glGetIntegerv( GL_VIEWPORT, viewport );
	aspect = (GLdouble)width() / (GLdouble)height();

	GLenum err;

	// Check for errors
	while ( ( err = glGetError() ) != GL_NO_ERROR )
		qDebug() << tr( "glview.cpp - GL ERROR (init) : " ) << getGLErrorString( int(err) );
}

void GLView::updateShaders()
{
	if ( !isValid() )
		return;
	auto	prvContext = pushGLContext();
	scene->updateShaders();
	popGLContext( prvContext );
	update();
}

void GLView::glProjection( [[maybe_unused]] int x, [[maybe_unused]] int y )
{
	if ( !scene->haveRenderer() )
		return;

	BoundSphere bs = scene->view * scene->bounds();

	if ( scene->hasOption(Scene::ShowAxes) ) {
		bs |= BoundSphere( scene->view * Vector3(), axis );
	}

	float bounds = std::max< float >( bs.radius, 1024.0f * scale() );


	GLdouble nr = std::fabs( bs.center[2] ) - bounds * 1.5;
	GLdouble fr = std::fabs( bs.center[2] ) + bounds * 1.5;

	if ( perspectiveMode || (view == ViewWalk) ) {
		// Perspective View
		if ( nr > fr ) {
			// add: swap them when needed
			std::swap( nr, fr );
		}
		nr = std::max< GLdouble >( nr, scale() );
		// ensure distance
		fr = std::max< GLdouble >( fr, nr + scale() );

		GLdouble h2 = std::tan( ( cfg.fov / Zoom ) / 360 * M_PI ) * nr;
		GLdouble w2 = h2 * aspect;
		scene->renderer->setProjectionMatrix( Matrix4::fromFrustum( -w2, +w2, -h2, +h2, nr, fr ) );
	} else {
		// Orthographic View
		GLdouble h2 = Dist / Zoom;
		GLdouble w2 = h2 * aspect;
		scene->renderer->setProjectionMatrix( Matrix4::fromOrtho( -w2, +w2, -h2, +h2, nr, fr ) );
	}
}


void GLView::paintGL()
{
#if DEBUG_FRAME_TIME
	auto	prvTime = std::chrono::steady_clock::now();
#endif

	updatePending = 0;

	glDisable( GL_FRAMEBUFFER_SRGB );
	glDepthMask( GL_TRUE );

	if ( isDisabled || !scene->haveRenderer() ) [[unlikely]] {
		glClearColor( cfg.background.red(), cfg.background.green(), cfg.background.blue(), cfg.background.alpha() );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
		return;
	}

	// Clear Viewport
	if ( scene->hasVisMode(Scene::VisSilhouette) ) {
		glClearColor( 1.0f, 1.0f, 1.0f, 1.0f );
	}

	bool	clearNeeded = true;
	if ( !perspectiveMode || doCompile ) {
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
		clearNeeded = false;
	}

	// Compile the model
	if ( doCompile ) [[unlikely]] {
		if ( doCompile > 1 ) [[unlikely]] {
			doCompile--;
			update();
			return;
		}
		// avoid potential infinite recursion in case a message box is opened while initializing the scene
		isDisabled = true;
		textures->setNifFolder( model->getFolder() );
		scene->make( model );
		scene->transform( Transform(), scene->timeMin() );

		axis = (scene->bounds().radius <= 0) ? 1024.0 * scale() : scene->bounds().radius;

		if ( scene->timeMin() != scene->timeMax() ) {
			if ( time < scene->timeMin() || time > scene->timeMax() )
				time = scene->timeMin();

			emit sequencesUpdated();

		} else if ( scene->timeMax() == 0 ) {
			// No Animations in this NIF
			emit sequencesDisabled( true );
		}
		emit sceneTimeChanged( time, scene->timeMin(), scene->timeMax() );
		isDisabled = false;
		doCompile = 0;
	}

	// Center the model
	if ( doCenter ) {
		setCenter();
		doCenter = false;
	}

	NifSkopeOpenGLContext *	cx = scene->renderer;

	// Transform the scene
	Transform	viewTrans;
	viewTrans.rotation.fromEuler( deg2rad(Rot[0]), deg2rad(Rot[1]), deg2rad(Rot[2]) );
	viewTrans.translation = viewTrans.rotation * Pos;
	if ( cfg.upAxis != ZAxis ) {
		float *	r = &( viewTrans.rotation( 0, 0 ) );
		if ( cfg.upAxis == XAxis ) {			// YZX -> XYZ
			FloatVector4::convertVector3( r ).shuffleValues( 0xD2 ).convertToVector3( r );
			FloatVector4::convertVector3( r + 3 ).shuffleValues( 0xD2 ).convertToVector3( r + 3 );
			FloatVector4::convertVector3( r + 6 ).shuffleValues( 0xD2 ).convertToVector3( r + 6 );
		} else if ( cfg.upAxis == YAxis ) {		// ZXY -> XYZ
			FloatVector4::convertVector3( r ).shuffleValues( 0xC9 ).convertToVector3( r );
			FloatVector4::convertVector3( r + 3 ).shuffleValues( 0xC9 ).convertToVector3( r + 3 );
			FloatVector4::convertVector3( r + 6 ).shuffleValues( 0xC9 ).convertToVector3( r + 6 );
		}
	}

	if ( view != ViewWalk )
		viewTrans.translation[2] -= Dist * 2;

	scene->transform( viewTrans, time );

	// Setup projection mode
	glProjection();

	cx->setViewTransform( scene->view, int( cfg.upAxis ), envMapRotation );
	auto &	globalUniforms = *( cx->globalUniforms );
	globalUniforms.toneMapScale = toneMapping;
	globalUniforms.brightnessScale = brightnessScale;
	globalUniforms.glowScale = ( scene->hasOption(Scene::DoGlow) ? glowScale : 0.0f );
	FloatVector4	mat_amb( 0.0f );
	FloatVector4	mat_diff( 0.0f );
	FloatVector4	lightDir( 0.0f, 0.0f, 1.0f, 0.0f );
	bool	drawLightPos = false;

	if ( scene->hasVisMode(Scene::VisSilhouette) ) {
		globalUniforms.brightnessScale = 0.0f;

	} else if ( scene->hasOption(Scene::DoLighting) ) {
		// Setup light

		if ( !frontalLight ) {
			Matrix m;
			m.fromEuler( deg2rad( declination ), 0.0f, deg2rad( planarAngle ) );
			lightDir = FloatVector4::convertVector3( m.data() + 6 );
			if ( cfg.upAxis == XAxis )
				lightDir.shuffleValues( 0xD2 );
			else if ( cfg.upAxis == YAxis )
				lightDir.shuffleValues( 0xC9 );
			globalUniforms.lightSourcePosition[0] = globalUniforms.viewMatrix[0] * lightDir[0];
			globalUniforms.lightSourcePosition[0] += globalUniforms.viewMatrix[1] * lightDir[1];
			globalUniforms.lightSourcePosition[0] += globalUniforms.viewMatrix[2] * lightDir[2];

			drawLightPos = scene->hasVisMode( Scene::VisLightPos );
		} else {
			globalUniforms.lightSourcePosition[0] = FloatVector4( 0.0f, 0.0f, 1.0f, 0.0f );
		}

		mat_amb = FloatVector4( ambient );

		//                       red 0 to 1   green 0 to 1  blue -1 to 0  green -1 to 0
		const FloatVector4	a6(  2.22062011f,  0.74144780f,  1.54254896f,  5.04086054f );
		const FloatVector4	a5( -8.61531450f, -2.99683819f,  1.07328175f, 15.77713878f );
		const FloatVector4	a4( 14.04554747f,  5.24041808f, -4.19602456f, 17.63027420f );
		const FloatVector4	a3(-12.84139010f, -5.38996620f, -4.89534064f,  7.70809183f );
		const FloatVector4	a2(  7.50629512f,  3.76745881f,  0.83151672f,  1.09740388f );
		const FloatVector4	a1( -2.98006874f, -1.86500227f,  3.00010002f,  1.26401749f );
		const FloatVector4	a0( 1.0f );
		FloatVector4	c( lightColor );
		c = ( ( ( ( (c * a6 + a5) * c + a4 ) * c + a3 ) * c + a2 ) * c + a1 ) * c + a0;
		c = ( lightColor < 0.0f ? c.shuffleValues( 0x2C ) : c.shuffleValues( 0xF4 ) );
		mat_diff = c.maxValues( FloatVector4(0.0f) ).minValues( FloatVector4(1.0f) ) * brightnessL;

	} else {
		mat_amb = FloatVector4( 7.0f );
		mat_diff = FloatVector4( 0.0f );
	}

	globalUniforms.lightSourceAmbient = mat_amb;
	globalUniforms.lightSourceDiffuse[0] = mat_diff;
	globalUniforms.glowScaleSRGB = float( std::sqrt( globalUniforms.glowScale ) );
	globalUniforms.doSkinning = std::int32_t( scene->hasOption(Scene::DoSkinning) );
	globalUniforms.sceneOptions = std::int32_t( scene->options );
	cx->setGlobalUniforms();

	cx->setDefaultVertexAttribs( Scene::defaultAttrMask, Scene::defaultVertexAttrs );

	if ( scene->hasOption(Scene::DoMultisampling) )
		glEnable( GL_MULTISAMPLE );

	if ( perspectiveMode ) {
		bool	colorBufCleared = scene->renderer->drawSkyBox( scene );
		if ( clearNeeded ) {
			glClear( colorBufCleared ? GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT
										: GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
		}
	}

	if ( drawLightPos ) {
		glEnable( GL_DEPTH_TEST );
		glDepthMask( GL_TRUE );
		glDepthFunc( GL_LESS );

		// Scale the distance a bit
		float	s = scale() * 64.0f;
		float	l = axis + s;
		l = (l < s * 2.0f) ? axis * 1.5f : l;
		l = (l > s * 32.0f) ? axis * 0.66f : l;
		l = (l > s * 16.0f) ? axis * 0.75f : l;
		lightDir = lightDir * l;

		scene->setGLColor( FloatVector4( 1.0f ) );
		scene->setGLLineWidth( Settings::lineWidthAxes * 0.5f );
		scene->loadModelViewMatrix( viewTrans );
		scene->drawDashLine( Vector3(), Vector3( lightDir ), 30 );
		scene->drawSphereSimple( Vector3( lightDir ), axis / 10.0f, 72, 6 );
	}

#ifndef QT_NO_DEBUG
	if ( debugMode == DbgBounds ) {
		// Debug scene bounds
		glEnable( GL_DEPTH_TEST );
		glDepthMask( GL_TRUE );
		glDepthFunc( GL_LESS );
		BoundSphere bs = scene->bounds();
		bs |= BoundSphere( Vector3(), axis );
		scene->loadModelViewMatrix( viewTrans );
		scene->setGLColor( 1.0f, 1.0f, 1.0f, 0.25f );
		scene->setGLLineWidth( Settings::lineWidthAxes );
		scene->drawSphereSimple( bs.center, bs.radius, 72, 6 );
	}

	// Color Key debug
	if ( debugMode == DbgColorPicker ) {
		glDisable( GL_MULTISAMPLE );
		glDisable( GL_LINE_SMOOTH );
		glDisable( GL_DITHER );
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LEQUAL );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		scene->selecting = ( scene->isSelModeVertex() ? 3 : 5 );
	} else {
		scene->selecting = 0;
	}
#endif

	// Draw the model
	glDisable( GL_BLEND );
	scene->draw();

	if ( scene->hasOption(Scene::ShowAxes) ) {
		// Resize viewport to small corner of screen
		int axesSize = int( std::min< double >( 0.1 * pixelWidth, 125.0 * devicePixelRatioF() ) + 0.5 );
		cx->setViewport( 0, 0, axesSize, axesSize );

		// Square frustum
		auto nr = 1.0;
		auto fr = 250.0;
		GLdouble h2 = std::tan( cfg.fov / 360 * M_PI ) * nr;
		GLdouble w2 = h2;
		if ( auto prog = scene->useProgram( "lines.prog" ); prog ) {
			cx->setProjectionMatrix( Matrix4::fromFrustum( -w2, +w2, -h2, +h2, nr, fr ) );
			cx->setGlobalUniforms();
		}

		// Zoom out slightly
		viewTrans.translation = { 0.0f, 0.0f, -150.0f };
		scene->loadModelViewMatrix( viewTrans );

		// Find direction of axes
		const auto & vtr = viewTrans.rotation;
		Vector3 axesDots( vtr( 2, 0 ), vtr( 2, 1 ), vtr( 2, 2 ) );

		scene->drawAxesOverlay( { 0.0f, 0.0f, 0.0f }, 50.0f, axesDots );

		// Restore viewport size
		cx->setViewport( 0, 0, pixelWidth, pixelHeight );
	}

	cx->stopProgram();
	cx->shrinkCache();

	// Check for errors
	GLenum err;
	while ( ( err = glGetError() ) != GL_NO_ERROR )
		qDebug() << tr( "glview.cpp - GL ERROR (paint): " ) << getGLErrorString( int(err) );

#if DEBUG_FRAME_TIME
	glFlush();
	glFinish();

	static float	frameTimes[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	static unsigned int	frameTimeIndex = 0;
	auto	t = std::chrono::steady_clock::now();
	double	dt = double( std::chrono::duration_cast< std::chrono::microseconds >( t - prvTime ).count() ) / 8000.0;
	frameTimes[frameTimeIndex & 7] = float( dt );
	frameTimeIndex++;
	float	avgTime = 0.0f;
	for ( int i = 0; i < 8; i++ )
		avgTime += frameTimes[i];
	std::fprintf( stderr, "Average frame time = %.2f ms\n", avgTime );
#endif

	emit paintUpdate();
}

void GLView::update()
{
	if ( !isExposed() ) {
		QOpenGLWindow::update();
	} else {
		// work around 5 ms delay to update()
		if ( !updatePending )
			QCoreApplication::postEvent( this, new QEvent( QEvent::UpdateRequest ), Qt::HighEventPriority );
		updatePending = 10;
	}
}


void GLView::resizeGL( int width, int height )
{
	pixelWidth = width;
	pixelHeight = height;

	if ( !isValid() )
		return;
	auto	prvContext = pushGLContext();

	aspect = GLdouble(width) / GLdouble(height);
	if ( !scene->renderer ) [[unlikely]]
		glViewport( 0, 0, width, height );
	else
		scene->renderer->setViewport( 0, 0, width, height );

	glDisable( GL_FRAMEBUFFER_SRGB );
	glClearColor( cfg.background.red(), cfg.background.green(), cfg.background.blue(), cfg.background.alpha() );

	popGLContext( prvContext );
}

void GLView::resizeEvent( QResizeEvent * e )
{
	double	p = devicePixelRatioF();
	resizeGL( int( p * e->size().width() + 0.5 ), int( p * e->size().height() + 0.5 ) );
}

void GLView::setFrontalLight( bool frontal )
{
	frontalLight = frontal;
	update();
}

static float convertBrightnessValue( int value )
{
	if ( value < 720 ) {
		// lower half of the slider range: sRGB curve from 0.0 to 1.0
		if ( value < 1 )
			return 0.0f;
		if ( value <= 29 )
			return float(value) / (720.0f * 12.92f);
		return float(std::pow((float(value) + 39.6f) / 759.6f, 2.4f));
	}
	// upper half of the slider range: exponential from 1.0 to 16.0
	if ( value == 720 )
		return 1.0f;
	if ( value >= 1440 )
		return 16.0f;
	return float(std::exp2(float(value - 720) / 180.0f));
}

void GLView::setBrightness( int value )
{
	brightnessScale = convertBrightnessValue( value );
	update();
}

void GLView::setLightLevel( int value )
{
	brightnessL = convertBrightnessValue( value );
	update();
}

void GLView::setLightColor( int value )
{
	lightColor = float( value ) / 720.0f - 1.0f;
	lightColor = lightColor * float( std::sqrt(std::fabs(lightColor)) );
	// color temperature = 6548.04 * exp(lightColor * 2.0401036)
	update();
}

void GLView::setToneMapping( int value )
{
	toneMapping = float( std::pow( 4.22978723f, float( value - 1440 ) / 720.0f ) );
	update();
}

void GLView::setAmbient( int value )
{
	ambient = convertBrightnessValue( value );
	update();
}

void GLView::setEnvMapRotation( int angle )
{
	envMapRotation = float( angle ) * 0.25f;	// Divide by 4 because sliders are -720 <-> 720
	update();
}

void GLView::setGlowScale( int value )
{
	glowScale = convertBrightnessValue( value );
	update();
}

void GLView::setDebugMode( DebugMode mode )
{
	debugMode = mode;
}

void GLView::setVisMode( Scene::VisMode mode, bool checked )
{
	if ( checked ) {
		scene->visMode |= mode;
	} else {
		if ( mode & scene->visMode & Scene::VisSilhouette ) {
			auto	prvContext = pushGLContext();
			glClearColor( cfg.background.red(), cfg.background.green(), cfg.background.blue(), cfg.background.alpha() );
			popGLContext( prvContext );
		}
		scene->visMode &= ~mode;
	}

	update();
}

typedef void (Scene::* DrawFunc)( void );

static int indexAt(
	NifModel * model, Scene * scene, QList<DrawFunc> drawFunc, const QPointF & pos, int & furn, bool shiftModifier )
{
	// Color Key O(1) selection
	//	Open GL 3.0 says glRenderMode is deprecated
	//	ATI OpenGL API implementation of GL_SELECT corrupts NifSkope memory
	//
	// Create FBO for sharp edges and no shading.
	// Texturing, blending, dithering, lighting and smooth shading should be disabled.
	// The FBO can be used for the drawing operations to keep the drawing operations invisible to the user.

	auto	context = scene->renderer;
	std::int32_t	viewport[4];
	context->getViewport().convertToInt32( viewport );

	// Create new FBO with multisampling disabled
	QOpenGLFramebufferObjectFormat fboFmt;
	fboFmt.setTextureTarget( GL_TEXTURE_2D );
	fboFmt.setInternalTextureFormat( GL_RGBA8 );
	fboFmt.setAttachment( QOpenGLFramebufferObject::Attachment::CombinedDepthStencil );

	QOpenGLFramebufferObject fbo( viewport[2], viewport[3], fboFmt );
	fbo.bind();

	float	savedClearColor[4];
	glGetFloatv( GL_COLOR_CLEAR_VALUE, savedClearColor );

	glDisable( GL_MULTISAMPLE );
	glDisable( GL_LINE_SMOOTH );
	glDisable( GL_POLYGON_SMOOTH );
	glDisable( GL_BLEND );
	glDisable( GL_DITHER );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	context->setGlobalUniforms();
	context->setDefaultVertexAttribs( Scene::defaultAttrMask, Scene::defaultVertexAttrs );

	// Rasterize the scene
	int	selectionFlags = int( Scene::SelObject );
	if ( scene->isSelModeVertex() )
		selectionFlags |= int( Scene::SelVertex );
	else if ( shiftModifier )
		selectionFlags |= int( Scene::SelTriangle );
	scene->selecting = (unsigned char) selectionFlags;
	for ( DrawFunc df : drawFunc ) {
		(scene->*df)();
	}
	scene->selecting = 0;

	context->stopProgram();
	context->shrinkCache();

	glClearColor( savedClearColor[0], savedClearColor[1], savedClearColor[2], savedClearColor[3] );
	glEnable( GL_DITHER );
	glEnable( GL_MULTISAMPLE );

	fbo.release();

	QImage img( fbo.toImage() );
	// disable premultiplied alpha
	img.reinterpretAsFormat( QImage::Format_ARGB32 );
	std::uint32_t pixel = std::uint32_t( img.pixel( pos.toPoint() ) );

#ifndef QT_NO_DEBUG
	img.save( "fbo.png" );
#endif

	// Convert BGRA to RGBA
	pixel = ( pixel & 0xFF00FF00U ) | ( ( pixel & 0xFFU ) << 16 ) | ( ( pixel >> 16 ) & 0xFFU );

	// Decode:
	// R = (id & 0x000000FF) >> 0
	// G = (id & 0x0000FF00) >> 8
	// B = (id & 0x00FF0000) >> 16
	// A = (id & 0xFF000000) >> 24

	int choose = getIDFromColorKey( pixel );

	// Pick BSFurnitureMarker
	if ( choose > 0 && selectionFlags == int( Scene::SelObject ) ) {
		int b = choose & 0x0ffff;
		int p = ( choose >> 16 ) & 0x0ffff;
		auto furnBlock = model->getBlockIndex( b, "BSFurnitureMarker" );

		if ( furnBlock.isValid() && model->getIndex( model->getIndex( furnBlock, "Positions" ), p ).isValid() ) {
			furn = p;
			choose = b;
		}
	}

	//qDebug() << "Key:" << a << " R" << pixel.red() << " G" << pixel.green() << " B" << pixel.blue();
	return choose;
}

QModelIndex GLView::indexAt( const QPointF & pos, bool shiftModifier )
{
	if ( !(model && isValid() && isVisible() && height() && scene->renderer) )
		return QModelIndex();

	QList<DrawFunc> df;

	if ( scene->hasOption(Scene::ShowCollision) )
		df << &Scene::drawHavok;

	if ( scene->hasOption(Scene::ShowNodes) )
		df << &Scene::drawNodes;

	if ( scene->hasOption(Scene::ShowMarkers) )
		df << &Scene::drawFurn;

	df << &Scene::drawShapes;

	auto	prvContext = pushGLContext();

	double	p = devicePixelRatioF();
	int	wp = pixelWidth;
	int	hp = pixelHeight;
	QPointF	posScaled( pos );
	posScaled *= p;
	scene->renderer->setViewport( 0, 0, wp, hp );
	glProjection( int( posScaled.x() + 0.5 ), int( posScaled.y() + 0.5 ) );

	int choose = -1, furn = -1;
	choose = ::indexAt( model, scene, df, posScaled, /*out*/ furn, shiftModifier );

	popGLContext( prvContext );

	QModelIndex chooseIndex;

	if ( scene->isSelModeVertex() ) {
		// Vertex
		int block = ( choose >> 16 ) & 0xFFFF;
		int vert = choose & 0xFFFF;

		auto shape = scene->shapes.value( block );
		if ( shape )
			chooseIndex = shape->vertexAt( vert );
	} else if ( choose >= 0 ) {
		// Block Index
		chooseIndex = model->getBlockIndex( !shiftModifier ? choose : ( choose & 0x7FFF ) );
		if ( shiftModifier ) {
			// Triangle
			if ( auto node = scene->getNode( scene->nifModel, chooseIndex ); node ) {
				if ( auto shape = dynamic_cast< Shape * >( node ); shape ) {
					auto	triangleIndex = shape->triangleAt( int( (unsigned int) choose >> 15 ) );
					if ( triangleIndex.isValid() )
						chooseIndex = triangleIndex;
				}
			}
		} else if ( furn != -1 ) {
			// Furniture Row @ Block Index
			chooseIndex = model->getIndex( model->getIndex( chooseIndex, "Positions" ), furn );
		}
	}

	return chooseIndex;
}

void GLView::center()
{
	doCenter = true;
	update();
}

void GLView::move( float x, float y, float z )
{
	Pos += Matrix::euler( deg2rad(Rot[0]), deg2rad(Rot[1]), deg2rad(Rot[2]) ).inverted() * Vector3( x, y, z );
	updateViewpoint();
	update();
}

void GLView::rotate( float x, float y, float z )
{
	FloatVector4	tmp( x, y, z, 0.0f );
	tmp += FloatVector4::convertVector3( &(Rot[0]) );
	( tmp - ( tmp / 360.0f ).roundValues() * 360.0f ).convertToVector3( &(Rot[0]) );	// wrap to -180.0 to 180.0
	updateViewpoint();
	update();
}

void GLView::rotateLight( float x, float z )
{
	declination += x;
	planarAngle -= z;
	declination -= float( roundFloat( declination / 360.0f ) ) * 360.0f;		// wrap to -180.0 to 180.0
	planarAngle -= float( roundFloat( planarAngle / 360.0f ) ) * 360.0f;
	lightVisTimer->start( lightVisTimeout );
	setVisMode( Scene::VisLightPos, true );
}

void GLView::setCenter()
{
	Node * node = scene->getNode( model, scene->currentBlock );

	if ( node ) {
		// Center on selected node
		BoundSphere bs = node->bounds();

		if ( bs.radius > 0 ) {
			Dist = bs.radius * 1.2;
		}

		this->setPosition( -bs.center );
	} else {
		// Center on entire mesh
		BoundSphere bs = scene->bounds();

		if ( bs.radius < scale() )
			bs.radius = 1024.0 * scale();

		Dist = bs.radius * 1.2;
		Zoom = 1.0;

		Pos = -bs.center;
	}
}

void GLView::setDistance( float x )
{
	Dist = x;
	update();
}

void GLView::setPosition( float x, float y, float z )
{
	Pos = { x, y, z };
	update();
}

void GLView::setPosition( const Vector3 & v )
{
	Pos = v;
	update();
}

void GLView::setProjection( bool isPersp )
{
	perspectiveMode = isPersp;
	update();
}

void GLView::setRotation( float x, float y, float z )
{
	Rot = { x, y, z };
	update();
}

void GLView::setZoom( float z )
{
	Zoom = std::min< float >( std::max< float >( z, ZOOM_MIN ), ZOOM_MAX );

	update();
}


void GLView::flipOrientation()
{
	ViewState tmp = ViewDefault;

	switch ( view ) {
	case ViewTop:
		tmp = ViewBottom;
		break;
	case ViewBottom:
		tmp = ViewTop;
		break;
	case ViewLeft:
		tmp = ViewRight;
		break;
	case ViewRight:
		tmp = ViewLeft;
		break;
	case ViewFront:
		tmp = ViewBack;
		break;
	case ViewBack:
		tmp = ViewFront;
		break;
	case ViewUser:
	default:
		view = tmp;
		if ( Node * node = scene->getNode( model, scene->currentBlock ); node )
			Pos = node->bounds().center * -2.0f - Pos;
		else
			Pos = scene->bounds().center * -2.0f - Pos;
		Rot[0] = ( Rot[0] < 0.0f ? -180.0f : 180.0f ) - Rot[0];
		Rot[1] *= -1.0f;
		Rot[2] = ( Rot[2] < 0.0f ? 180.0f : -180.0f ) + Rot[2];
		update();
		return;
	}

	setOrientation( tmp, false );
}

void GLView::setOrientation( GLView::ViewState state, bool recenter )
{
	if ( state == view )
		return;

	if ( int i = int( state ) - int( ViewTop ); i >= 0 && i <= 5 ) {
		Rot = viewRotations[i];
		update();
	}

	view = state;

	// Recenter
	if ( recenter )
		center();
}

void GLView::updateViewpoint()
{
	switch ( view ) {
	case ViewTop:
	case ViewBottom:
	case ViewLeft:
	case ViewRight:
	case ViewFront:
	case ViewBack:
	case ViewUser:
		emit viewpointChanged();
		break;
	default:
		break;
	}
}

void GLView::flush()
{
	if ( textures )
		textures->flush();
}


/*
 *  NifModel
 */

void GLView::setNif( NifModel * nif )
{
	if ( model ) {
		// disconnect( model ) may not work with new Qt5 syntax...
		// it says the calls need to remain symmetric to the connect() ones.
		// Otherwise, use QMetaObject::Connection
		disconnect( model );
	}

	model = nif;

	if ( model ) {
		connect( model, &NifModel::dataChanged, this, &GLView::dataChanged );
		connect( model, &NifModel::linksChanged, this, &GLView::modelLinked );
		connect( model, &NifModel::modelReset, this, &GLView::modelChanged );
		connect( model, &NifModel::destroyed, this, &GLView::modelDestroyed );
		Dist = ( model->getBSVersion() < 170 ? 1228.8f : 19.2f );
	}

	doCompile = 2;
}

void GLView::setCurrentIndex( const QModelIndex & index )
{
	if ( !( model && index.model() == model ) )
		return;

	scene->currentBlock = model->getBlockIndex( index );
	scene->currentIndex = index.sibling( index.row(), 0 );

	update();
}

QModelIndex parent( QModelIndex ix, QModelIndex xi )
{
	ix = ix.sibling( ix.row(), 0 );
	xi = xi.sibling( xi.row(), 0 );

	while ( ix.isValid() ) {
		QModelIndex x = xi;

		while ( x.isValid() ) {
			if ( ix == x )
				return ix;

			x = x.parent();
		}

		ix = ix.parent();
	}

	return QModelIndex();
}

void GLView::dataChanged( const QModelIndex & idx, const QModelIndex & xdi )
{
	if ( doCompile )
		return;

	QModelIndex ix = idx;

	if ( idx == xdi ) {
		if ( idx.column() != 0 )
			ix = idx.sibling( idx.row(), 0 );
	} else {
		ix = ::parent( idx, xdi );
	}

	if ( ix.isValid() ) {
		scene->update( model, idx );
		update();
	} else {
		modelChanged();
	}
}

void GLView::modelChanged()
{
	if ( doCompile )
		return;

	doCompile = 1;
	//doCenter  = true;
	update();
}

void GLView::modelLinked()
{
	if ( doCompile )
		return;

	doCompile = 1; //scene->update( model, QModelIndex() );
	update();
}

void GLView::modelDestroyed()
{
	setNif( nullptr );
}


/*
 * UI
 */

void GLView::setSceneTime( float t )
{
	time = t;
	update();
	emit sceneTimeChanged( time, scene->timeMin(), scene->timeMax() );
}

void GLView::setSceneSequence( const QString & seqname )
{
	// Update UI
	QAction * action = qobject_cast<QAction *>(sender());
	if ( !action ) {
		// Called from self and not UI
		emit sequenceChanged( seqname );
	}

	scene->setSequence( seqname );
	time = scene->timeMin();
	emit sceneTimeChanged( time, scene->timeMin(), scene->timeMax() );
	update();
}

// TODO: Multiple user views, ala Recent Files
void GLView::saveUserView()
{
	QSettings settings;
	settings.beginGroup( "GLView" );
	settings.beginGroup( "User View" );
	settings.setValue( "RotX", Rot[0] );
	settings.setValue( "RotY", Rot[1] );
	settings.setValue( "RotZ", Rot[2] );
	settings.setValue( "PosX", Pos[0] );
	settings.setValue( "PosY", Pos[1] );
	settings.setValue( "PosZ", Pos[2] );
	settings.setValue( "Dist", Dist );
	settings.endGroup();
	settings.endGroup();
}

void GLView::loadUserView()
{
	QSettings settings;
	settings.beginGroup( "GLView" );
	settings.beginGroup( "User View" );
	setRotation( settings.value( "RotX" ).toDouble(), settings.value( "RotY" ).toDouble(), settings.value( "RotZ" ).toDouble() );
	setPosition( settings.value( "PosX" ).toDouble(), settings.value( "PosY" ).toDouble(), settings.value( "PosZ" ).toDouble() );
	setDistance( settings.value( "Dist" ).toDouble() );
	settings.endGroup();
	settings.endGroup();
}

inline bool GLView::kbd( int n ) const
{
	return bool( kbdState & ( 1ULL << n ) );
}

void GLView::advanceGears()
{
	updatePending -= (unsigned char) bool( updatePending );

	std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
	float dT = float( std::chrono::duration_cast< std::chrono::microseconds >( t - lastTime ).count() ) * 0.000001f;
	lastTime = t;

	if ( !isVisible() )
		return;

	dT = std::clamp< float >( dT, 0.0f, 1.0f );
	if ( ( animState & AnimEnabled ) && ( animState & AnimPlay )
		&& scene->timeMin() != scene->timeMax() )
	{
		time += dT;

		if ( time > scene->timeMax() ) {
			if ( ( animState & AnimSwitch ) && !scene->animGroups.isEmpty() ) {
				int ix = scene->animGroups.indexOf( scene->animGroup );

				if ( ++ix >= scene->animGroups.count() )
					ix -= scene->animGroups.count();

				setSceneSequence( scene->animGroups.value( ix ) );
			} else if ( animState & AnimLoop ) {
				time = scene->timeMin();
			} else {
				// Animation has completed and is not looping
				//	or cycling through animations.
				// Reset time and state and then inform UI it has stopped.
				time = scene->timeMin();
				animState &= ~AnimPlay;
				emit sequenceStopped();
			}
		} else {
			// Animation is not done yet
		}

		emit sceneTimeChanged( time, scene->timeMin(), scene->timeMax() );
		update();
	}

	float	rotateStep = cfg.rotSpd * dT;
	// Fix movement speed for Starfield scale
	dT *= scale();
	float	moveStep = cfg.moveSpd * dT;

	// TODO: Some kind of input class for choosing the appropriate
	// keys based on user preferences of what app they would like to
	// emulate for the control scheme
	// Rotation
	if ( kbd( Key_Shift ) && !frontalLight ) {
		if ( kbd( Key_RotateUp ) )    rotateLight( -rotateStep, 0.0f );
		if ( kbd( Key_RotateDown ) )  rotateLight( rotateStep, 0.0f );
		if ( kbd( Key_RotateLeft ) )  rotateLight( 0.0f, -rotateStep );
		if ( kbd( Key_RotateRight ) ) rotateLight( 0.0f, rotateStep );
	} else {
		if ( kbd( Key_RotateUp ) )    rotate( -rotateStep, 0, 0 );
		if ( kbd( Key_RotateDown ) )  rotate( rotateStep, 0, 0 );
		if ( kbd( Key_RotateLeft ) )  rotate( 0, 0, -rotateStep );
		if ( kbd( Key_RotateRight ) ) rotate( 0, 0, rotateStep );
	}

	// Movement
	if ( kbd( Key_MoveLeft ) ) move( moveStep, 0, 0 );
	if ( kbd( Key_MoveRight ) ) move( -moveStep, 0, 0 );
	if ( kbd( Key_MoveForward ) ) move( 0, 0, moveStep );
	if ( kbd( Key_MoveBack ) ) move( 0, 0, -moveStep );
	if ( kbd( Key_MoveDown ) ) move( 0, moveStep, 0 );
	if ( kbd( Key_MoveUp ) ) move( 0, -moveStep, 0 );

	// Focal Length
	if ( kbd( Key_ZoomIn ) )   setZoom( Zoom * std::sqrt( Settings::zoomOutScale ) );
	if ( kbd( Key_ZoomOut ) )  setZoom( Zoom * std::sqrt( Settings::zoomInScale ) );

	if ( mouseMov[0] != 0 || mouseMov[1] != 0 || mouseMov[2] != 0 ) {
		move( mouseMov[0], mouseMov[1], mouseMov[2] );
		mouseMov = Vector3();
	}

	if ( mouseRot[0] != 0 || mouseRot[1] != 0 || mouseRot[2] != 0 ) {
		rotate( mouseRot[0], mouseRot[1], mouseRot[2] );
		mouseRot = Vector3();
	}

	// update display without movement
	if ( kbd( Key_Update ) ) update();
}


// TODO: Separate widget
void GLView::saveImage()
{
	auto dlg = new QDialog( qApp->activeWindow() );
	QGridLayout * lay = new QGridLayout( dlg );
	dlg->setWindowTitle( tr( "Save View" ) );
	dlg->setLayout( lay );
	dlg->setMinimumWidth( 400 );

	// Save file format, quality and default screenshot path
	int imgFormat, jpegQuality;
	QString imgPath;
	{
		QSettings settings;
		jpegQuality = settings.value( "JPEG/Quality", 90 ).toInt();
		imgFormat = settings.value( "Screenshot/Format", 0 ).toInt();
		imgFormat = std::clamp< int >( imgFormat, 0, 4 );
		imgPath = settings.value( "Screenshot/Folder", "screenshots" ).toString();
	}

	QString date = QDateTime::currentDateTime().toString( "yyyyMMdd_HH-mm-ss" );
	QString name = model->getFilename();

	QString nifFolder = model->getFolder();
	static const char *	screenshotImgFormats[5] = {
		".jpg", ".png", ".webp", ".bmp", ".dds"
	};
	QString filename = name + (!name.isEmpty() ? "_" : "") + date + screenshotImgFormats[imgFormat];

	// Default: NifSkope directory
	QString nifskopePath = "screenshots/" + filename;
	// Absolute: NIF directory
	QString nifPath = nifFolder + (!nifFolder.isEmpty() ? "/" : "") + filename;

	FileSelector * file = new FileSelector( FileSelector::SaveFile, tr( "File" ), QBoxLayout::LeftToRight );
	file->setParent( dlg );
	file->setFilter( { "Images (*.jpg *.png *.webp *.bmp *.dds)", "JPEG (*.jpg)", "PNG (*.png)", "WebP (*.webp)", "BMP (*.bmp)", "DDS (*.dds)" } );
	file->setFile( imgPath + "/" + filename  );
	lay->addWidget( file, 0, 0, 1, -1 );

	QPushButton * nifskopeDir = new QPushButton( tr( "NifSkope Directory" ), dlg );
	nifskopeDir->setToolTip( tr( "Save to NifSkope screenshots directory" ) );

	QPushButton * niffileDir = new QPushButton( tr( "NIF Directory" ), dlg );
	niffileDir->setDisabled( nifFolder.isEmpty() );
	niffileDir->setToolTip( tr( "Save to NIF file directory" ) );

	lay->addWidget( nifskopeDir, 1, 0, 1, 1 );
	lay->addWidget( niffileDir, 1, 1, 1, 1 );

	QHBoxLayout * pixBox = new QHBoxLayout;
	pixBox->setAlignment( Qt::AlignRight );
	QSpinBox * pixQuality = new QSpinBox( dlg );
	pixQuality->setRange( -1, 100 );
	pixQuality->setSingleStep( 10 );
	pixQuality->setValue( jpegQuality );
	pixQuality->setSpecialValueText( tr( "Auto" ) );
	pixQuality->setMaximumWidth( pixQuality->minimumSizeHint().width() );
	pixBox->addWidget( new QLabel( tr( "JPEG Quality" ), dlg ) );
	pixBox->addWidget( pixQuality );
	lay->addLayout( pixBox, 1, 2, Qt::AlignRight );


	// Get max viewport size for platform
	GLint	dims[2];
	glGetIntegerv( GL_MAX_VIEWPORT_DIMS, dims );

	// Default size
	auto btnOneX = new QRadioButton( "1x", dlg );
	btnOneX->setCheckable( true );
	btnOneX->setChecked( true );
	// Disable any of these that would exceed the max viewport size of the platform
	int	w = width();
	int	h = height();
	double	p = devicePixelRatioF();
	QRadioButton	*btnTwoX, *btnFourX, *btnEightX;
	for ( int i = 1; i <= 3; i++ ) {
		QRadioButton* &	b = ( i == 1 ? btnTwoX : ( i == 2 ? btnFourX : btnEightX ) );
		b = new QRadioButton( ( i == 1 ? "2x" : ( i == 2 ? "4x" : "8x" ) ), dlg );
		b->setCheckable( true );
		int	wp = int( p * ( w << i ) + 0.5 );
		int	hp = int( p * ( h << i ) + 0.5 );
		b->setDisabled( wp > dims[0] || hp > dims[1] );
	}


	auto grpBox = new QGroupBox( tr( "Image Size" ), dlg );
	auto grpBoxLayout = new QHBoxLayout;
	grpBoxLayout->addWidget( btnOneX );
	grpBoxLayout->addWidget( btnTwoX );
	grpBoxLayout->addWidget( btnFourX );
	grpBoxLayout->addWidget( btnEightX );
	grpBoxLayout->addWidget( new QLabel( "<b>Caution:</b><br/> 4x and 8x may be memory intensive.", dlg ) );
	grpBoxLayout->addStretch( 1 );
	grpBox->setLayout( grpBoxLayout );

	auto grpSize = new QButtonGroup( dlg );
	grpSize->addButton( btnOneX, 1 );
	grpSize->addButton( btnTwoX, 2 );
	grpSize->addButton( btnFourX, 4 );
	grpSize->addButton( btnEightX, 8 );

	grpSize->setExclusive( true );

	lay->addWidget( grpBox, 2, 0, 1, -1 );


	QHBoxLayout * hBox = new QHBoxLayout;
	QPushButton * btnOk = new QPushButton( tr( "Save" ), dlg );
	QPushButton * btnCancel = new QPushButton( tr( "Cancel" ), dlg );
	hBox->addWidget( btnOk );
	hBox->addWidget( btnCancel );
	lay->addLayout( hBox, 3, 0, 1, -1 );

	// Set FileSelector to NifSkope dir (relative)
	connect( nifskopeDir, &QPushButton::clicked, [=]()
		{
			file->setText( nifskopePath );
			file->setFile( nifskopePath );
		}
	);
	// Set FileSelector to NIF File dir (absolute)
	connect( niffileDir, &QPushButton::clicked, [=]()
		{
			file->setText( nifPath );
			file->setFile( nifPath );
		}
	);

	// Validate on OK
	connect( btnOk, &QPushButton::clicked, [&]()
		{
			imgPath = file->file();
			for ( imgFormat = int( sizeof( screenshotImgFormats ) / sizeof( char * ) ); --imgFormat > 0; ) {
				if ( imgPath.endsWith( screenshotImgFormats[imgFormat], Qt::CaseInsensitive ) )
					break;
			}
#ifdef Q_OS_WIN32
			imgPath.replace( QChar('\\'), QChar('/') );
#endif
			imgPath.truncate( imgPath.lastIndexOf( QChar('/') ) );

			// Save JPEG Quality and other settings
			QSettings settings;
			settings.setValue( "JPEG/Quality", pixQuality->value() );
			settings.setValue( "Screenshot/Format", imgFormat );
			if ( !imgPath.isEmpty() )
				settings.setValue( "Screenshot/Folder", imgPath );

			// Supersampling
			int	ss = grpSize->checkedId();

			auto	prvContext = pushGLContext();

			// Resize viewport for supersampling
			if ( ss > 1 )
				resizeGL( int( p * ( w * ss ) + 0.5 ), int( p * ( h * ss ) + 0.5 ) );

			QSize	fboSize( getSizeInPixels() );
			auto	savedSceneOptions = scene->options;
			auto	savedSceneVisMode = scene->visMode;
			bool	haveAlpha = ( imgFormat == 1 || imgFormat == 4 );	// PNG or DDS
			bool	useSilhouette = ( imgFormat == 1 );
			std::string	err;

			QImage	rgbImg;
			QImage	alphaImg;
			const Color4 & c = cfg.background;
			try {
				for ( int i = 0; i <= int( useSilhouette ); i++ ) {
					QOpenGLFramebufferObjectFormat fboFmt;
					fboFmt.setTextureTarget( GL_TEXTURE_2D );
					fboFmt.setInternalTextureFormat( i == 0 ? GL_SRGB8_ALPHA8 : GL_RGBA8 );
					fboFmt.setMipmap( false );
					fboFmt.setAttachment( QOpenGLFramebufferObject::Attachment::Depth );
					fboFmt.setSamples( 16 / ss );

					QOpenGLFramebufferObject fbo( fboSize.width(), fboSize.height(), fboFmt );
					fbo.bind();

					if ( haveAlpha ) {
						if ( !useSilhouette )
							glClearColor( c.red(), c.green(), c.blue(), 0.0f );
						scene->options = savedSceneOptions & ~( Scene::ShowAxes | Scene::ShowGrid );
						if ( i )
							scene->visMode = Scene::VisSilhouette;
					}
					paintGL();

					fbo.release();

					( i == 0 ? rgbImg : alphaImg ) = fbo.toImage();
				}
			} catch ( std::exception & e ) {
				err = e.what();
			}

			// Restore settings and return viewport to original size
			scene->options = savedSceneOptions;
			scene->visMode = savedSceneVisMode;
			glClearColor( c.red(), c.green(), c.blue(), c.alpha() );
			if ( ss > 1 )
				resizeGL( int( p * w + 0.5 ), int( p * h + 0.5 ) );

			popGLContext( prvContext );

			if ( !err.empty() ) {
				QMessageBox::critical( nullptr, "NifSkope error", QString::fromStdString( err ) );
				return;
			}

			rgbImg.reinterpretAsFormat( !haveAlpha ? QImage::Format_RGB32 : QImage::Format_ARGB32 );
			int	imgWidth = rgbImg.bytesPerLine() >> 2;
			int	imgHeight = rgbImg.height();

			for ( int y = 0; useSilhouette && y < imgHeight; y++ ) {
				// Combine RGB image with alpha mask from silhouette (FIXME: possible byte order issues)
				if ( !( alphaImg.width() >= rgbImg.width() && y < alphaImg.height() ) ) [[unlikely]]
					continue;
				std::uint32_t *	rgbPtr = reinterpret_cast< std::uint32_t * >( rgbImg.scanLine( y ) );
				const std::uint32_t *	alphaPtr =
					reinterpret_cast< const std::uint32_t * >( alphaImg.constScanLine( y ) );
				for ( int x = 0; x < imgWidth; x++ ) {
					FloatVector4	rgba( rgbPtr + x );
					FloatVector4	a( alphaPtr + x );
					rgba[3] = a.dotProduct3( FloatVector4( -1.0f / 3.0f ) ) + 255.0f;
					rgbPtr[x] = std::uint32_t( rgba );
				}
			}

			try {
				if ( imgFormat != 4 ) {
					QImageWriter writer( file->file() );

					// Set Compression for formats that can use it
					writer.setCompression( 1 );

					// Handle JPEG/WebP Quality
					writer.setFormat( screenshotImgFormats[imgFormat] + 1 );
					int	q = pixQuality->value();
					if ( q < 0 )
						q = 75;
					switch ( imgFormat ) {
					case 0:	// JPEG
						writer.setQuality( 50 + q / 2 );
						writer.setOptimizedWrite( true );
						writer.setProgressiveScanWrite( true );
						break;
					case 1:	// PNG
						writer.setCompression( q );
						break;
					case 2:	// WebP
						writer.setQuality( 50 + q / 2 );
						break;
					}

					if ( !writer.write( rgbImg ) )
						throw NifSkopeError( "%s", writer.errorString().toStdString().c_str() );

				} else {	// DDS
					DDSOutputFile	writer( file->file().toStdString().c_str(), imgWidth, imgHeight,
											DDSInputFile::pixelFormatRGBA32 );
					// TODO: portable handling of byte order
					writer.writeData( rgbImg.constBits(), size_t( rgbImg.sizeInBytes() ) );
				}

				dlg->accept();

			} catch ( std::exception & e ) {
				QMessageBox::critical( nullptr, "NifSkope error", tr( "Could not save %1: %2" ).arg( file->file() ).arg( e.what() ) );
			}
		}
	);
	connect( btnCancel, &QPushButton::clicked, dlg, &QDialog::reject );

	if ( dlg->exec() != QDialog::Accepted ) {
		return;
	}
}


/*
 * QWidget Event Handlers
 */

void GLView::contextMenuEvent( QContextMenuEvent * e )
{
	if ( ( pressPos - lastPos ).manhattanLength() <= 10 ) {
		mouseButtonState = 0;
		contextMenuShiftModifier = bool( e->modifiers() & Qt::ShiftModifier );
		emit graphicsView->customContextMenuRequested( e->pos() );
		e->accept();
	}
}

void GLView::dragEnterEvent( QDragEnterEvent * e )
{
	// Intercept NIF files
	if ( e->mimeData()->hasUrls() ) {
		QList<QUrl> urls = e->mimeData()->urls();
		for ( auto url : urls ) {
			if ( url.scheme() == "file" ) {
				QString fn = url.toLocalFile();
				QFileInfo finfo( fn );
				if ( finfo.exists() && NifSkope::fileExtensions().contains( finfo.suffix(), Qt::CaseInsensitive ) ) {
					draggedNifs << finfo.absoluteFilePath();
				}
			}
		}

		if ( !draggedNifs.isEmpty() ) {
			e->accept();
			return;
		}
	}

	auto md = e->mimeData();
	if ( md && md->hasUrls() && md->urls().count() == 1 ) {
		QUrl url = md->urls().first();

		if ( url.scheme() == "file" ) {
			QString fn = url.toLocalFile();

			if ( textures->canLoad( fn ) ) {
				fnDragTex = textures->stripPath( fn, model->getFolder() );
				e->accept();
				return;
			}
		}
	}

	e->ignore();
}

void GLView::dragLeaveEvent( QDragLeaveEvent * e )
{
	if ( !draggedNifs.isEmpty() ) {
		draggedNifs.clear();
		e->ignore();
		return;
	}

	if ( iDragTarget.isValid() ) {
		model->set<QString>( iDragTarget, fnDragTexOrg );
		iDragTarget = QModelIndex();
		fnDragTex = fnDragTexOrg = QString();
	}
}

void GLView::dragMoveEvent( QDragMoveEvent * e )
{
	if ( !draggedNifs.isEmpty() ) {
		e->accept();
		return;
	}

	if ( iDragTarget.isValid() ) {
		model->set<QString>( iDragTarget, fnDragTexOrg );
		iDragTarget  = QModelIndex();
		fnDragTexOrg = QString();
	}

	QModelIndex iObj = model->getBlockIndex( indexAt( e->posF() ), "NiAVObject" );

	if ( iObj.isValid() ) {
		for ( const auto l : model->getChildLinks( model->getBlockNumber( iObj ) ) ) {
			QModelIndex iTxt = model->getBlockIndex( l, "NiTexturingProperty" );

			if ( iTxt.isValid() ) {
				QModelIndex iSrc = model->getBlockIndex( model->getLink( iTxt, "Base Texture/Source" ), "NiSourceTexture" );

				if ( iSrc.isValid() ) {
					iDragTarget = model->getIndex( iSrc, "File Name" );

					if ( iDragTarget.isValid() ) {
						fnDragTexOrg = model->get<QString>( iDragTarget );
						model->set<QString>( iDragTarget, fnDragTex );
						e->accept();
						return;
					}
				}
			}
		}
	}

	e->ignore();
}

void GLView::dropEvent( QDropEvent * e )
{
	if ( !draggedNifs.isEmpty() ) {
		auto ns = qobject_cast<NifSkope *>( graphicsView->parent() );
		if ( ns )
			ns->openFiles( draggedNifs );

		draggedNifs.clear();
		e->accept();
		return;
	}

	iDragTarget = QModelIndex();
	fnDragTex = fnDragTexOrg = QString();
	e->accept();
}

void GLView::focusOutEvent( QFocusEvent * )
{
	kbdState = 0;
	mouseButtonState = 0;
}

int GLView::convertKeyCode( int n ) const
{
	switch ( n ) {
	case Qt::Key_Up:
		return Key_RotateUp;
	case Qt::Key_Down:
		return Key_RotateDown;
	case Qt::Key_Left:
		return Key_RotateLeft;
	case Qt::Key_Right:
		return Key_RotateRight;
	case Qt::Key_PageUp:
		return Key_ZoomIn;
	case Qt::Key_PageDown:
		return Key_ZoomOut;
	case Qt::Key_A:
		return Key_MoveLeft;
	case Qt::Key_D:
		return Key_MoveRight;
	case Qt::Key_W:
		return Key_MoveForward;
	case Qt::Key_S:
		return Key_MoveBack;
#if 0
	case Qt::Key_F:
		return Key_FrontView;
#endif
	case Qt::Key_Q:
		return Key_MoveDown;
	case Qt::Key_E:
		return Key_MoveUp;
	case Qt::Key_M:
		return Key_Update;
	case Qt::Key_Space:
		return Key_MoveCam;
	case Qt::Key_Shift:
		return Key_Shift;
	}
	return -1;
}

void GLView::keyPressEvent( QKeyEvent * event )
{
	int	k = convertKeyCode( event->key() );
	if ( k >= 0 ) {
		kbdState = kbdState | ( 1ULL << k );
		if ( k != Key_Shift )
			return;
	} else {
		switch ( event->key() ) {
		case Qt::Key_Escape:
			doCompile = 1;

			if ( view == ViewWalk )
				doCenter = true;

			update();
			break;
		case Qt::Key_F:
		case Qt::Key_L:
		case Qt::Key_T:
			if ( event->modifiers() & Qt::ShiftModifier ) {
				if ( event->key() == Qt::Key_F ) {
					if ( !frontalLight ) {
						frontalLight = true;
						emit frontalLightChanged( true );
						update();
					}
				} else {
					float	d = ( event->key() == Qt::Key_T ? 0.0f : 90.0f );
					declination = d;
					planarAngle = d;
					if ( frontalLight ) {
						frontalLight = false;
						emit frontalLightChanged( false );
					}
					update();
				}
				return;
			}
			break;
		default:
			break;
		}
	}
	event->ignore();
}

void GLView::keyReleaseEvent( QKeyEvent * event )
{
	int	k = convertKeyCode( event->key() );
	if ( k >= 0 ) {
		kbdState = kbdState & ~( 1ULL << k );
		if ( k != Key_Shift )
			return;
	}
	event->ignore();
}

void GLView::mouseDoubleClickEvent( QMouseEvent * )
{
	/*
	doCompile = 1;
	if ( ! aViewWalk->isChecked() )
	doCenter = true;
	update();
	*/
}

void GLView::mouseMoveEvent( QMouseEvent * event )
{
	auto	newPos = event->localPos();
	float	dx = newPos.x() - lastPos.x();
	float	dy = newPos.y() - lastPos.y();
	Qt::MouseButtons	buttonMask = Qt::MouseButtons( mouseButtonState );

	if ( ( buttonMask | event->buttons() ) != buttonMask ) [[unlikely]] {
		// work around button events being lost after activating the context menu
		buttonMask = buttonMask | event->buttons();
		mouseButtonState = std::uint32_t( buttonMask );
		dx = 0.0f;
		dy = 0.0f;
	}

	if ( ( buttonMask & Qt::LeftButton ) && !kbd( Key_MoveCam ) ) {
		if ( !frontalLight && ( event->modifiers() & Qt::ShiftModifier ) )
			rotateLight( dy * 0.5f, dx * 0.5f );
		else
			mouseRot += Vector3( dy * 0.5f, 0.0f, dx * 0.5f );
	} else if ( ( buttonMask & Qt::MiddleButton ) || ( ( buttonMask & Qt::LeftButton ) && kbd( Key_MoveCam ) ) ) {
		float d = axis / (qMax( width(), height() ) + 1);
		mouseMov += Vector3( dx * d, -dy * d, 0.0f );
	} else if ( buttonMask & Qt::RightButton ) {
		setDistance( Dist - (dx + dy) * (axis / (qMax( width(), height() ) + 1)) );
	}

	lastPos = newPos;
}

void GLView::mousePressEvent( QMouseEvent * event )
{
	mouseButtonState |= std::uint32_t( event->button() );
	if ( event->button() == Qt::ForwardButton || event->button() == Qt::BackButton ) {
		event->ignore();
		return;
	}

	lastPos = event->localPos();

	pressPos = lastPos;
}

void GLView::mouseReleaseEvent( QMouseEvent * event )
{
	mouseButtonState &= ~( std::uint32_t( event->button() ) );

#ifdef Q_OS_LINUX
	bool	isColorPicker = bool( event->modifiers() & ( Qt::AltModifier | Qt::ControlModifier ) );
#else
	bool	isColorPicker = bool( event->modifiers() & Qt::AltModifier );
#endif
	if ( model && (pressPos - event->localPos()).manhattanLength() <= 3 ) {
		if ( event->button() == Qt::ForwardButton || event->button() == Qt::BackButton
			|| event->button() == Qt::MiddleButton ) {
			event->ignore();
			return;
		}

		if ( !isColorPicker ) {
			QModelIndex idx = indexAt( event->localPos(), bool( event->modifiers() & Qt::ShiftModifier ) );
			scene->currentBlock = model->getBlockIndex( idx );
			scene->currentIndex = idx.sibling( idx.row(), 0 );

			if ( idx.isValid() ) {
#if 0
				// this makes vertex selection slow, and may no longer be needed with newer Qt versions
				emit clicked( QModelIndex() ); // HACK: To get Block Details to update
#endif
				emit clicked( idx );
			}

		} else {
			// Color Picker / Eyedrop tool
			auto	prvContext = pushGLContext();
			{
				QOpenGLFramebufferObjectFormat fboFmt;
				fboFmt.setTextureTarget( GL_TEXTURE_2D );
				fboFmt.setInternalTextureFormat( GL_SRGB8 );
				fboFmt.setMipmap( false );
				fboFmt.setAttachment( QOpenGLFramebufferObject::Attachment::Depth );

				QOpenGLFramebufferObject fbo( pixelWidth, pixelHeight, fboFmt );
				fbo.bind();

				paintGL();

				fbo.release();

				QImage img( fbo.toImage() );

				QColor what = QColor( img.pixel( ( event->localPos() * devicePixelRatioF() ).toPoint() ) );

				glClearColor( what.redF(), what.greenF(), what.blueF(), what.alphaF() );
				// qDebug() << what;
			}
			popGLContext( prvContext );
		}

		update();
	}

	if ( event->button() == Qt::RightButton && !isColorPicker ) {
		QContextMenuEvent	e( QContextMenuEvent::Mouse, event->pos(), event->globalPos(), event->modifiers() );
		contextMenuEvent( &e );
	}
}

void GLView::wheelEvent( QWheelEvent * event )
{
	if ( view == ViewWalk ) {
		mouseMov += Vector3( 0, 0, double( event->angleDelta().y() ) / 4.0 ) * scale();
	} else {
		if (event->angleDelta().y() < 0)
			setDistance( Dist * Settings::zoomOutScale );
		else
			setDistance( Dist * Settings::zoomInScale );
	}
}

const char * GLView::getGLErrorString( int err )
{
	switch ( err ) {
	case GL_NO_ERROR:
		return "No Error";
	case GL_INVALID_ENUM:
		return "GL_INVALID_ENUM";
	case GL_INVALID_VALUE:
		return "GL_INVALID_VALUE";
	case GL_INVALID_OPERATION:
		return "GL_INVALID_OPERATION";
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		return "GL_INVALID_FRAMEBUFFER_OPERATION";
	case GL_OUT_OF_MEMORY:
		return "GL_OUT_OF_MEMORY";
	case GL_STACK_UNDERFLOW:
		return "GL_STACK_UNDERFLOW";
	case GL_STACK_OVERFLOW:
		return "GL_STACK_OVERFLOW";
	}
	return "Unknown OpenGL Error";
}
