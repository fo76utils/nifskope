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
#include "libfo76utils/src/fp32vec4.hpp"
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
#include <QTimer>
#include <QToolBar>

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QGLFormat>


// NOTE: The FPS define is a frame limiter,
//	NOT the guaranteed FPS in the viewport.
//	Also the QTimer is integer milliseconds
//	so 60 will give you 1000/60 = 16, not 16.666
//	therefore it's really 62.5FPS
#define FPS 144

#define ZOOM_MIN 1.0
#define ZOOM_MAX 1000.0


//! @file glview.cpp GLView implementation


GLGraphicsView::GLGraphicsView( QWidget * parent ) : QGraphicsView()
{
	setContextMenuPolicy( Qt::CustomContextMenu );
	setFocusPolicy( Qt::ClickFocus );
	setAcceptDrops( true );

	installEventFilter( parent );
}

GLGraphicsView::~GLGraphicsView() {}


GLView * GLView::create( NifSkope * window )
{
	QGLFormat fmt;
	static QList<QPointer<GLView> > views;

	QGLWidget * share = nullptr;
	for ( const QPointer<GLView>& v : views ) {
		if ( v )
			share = v;
	}

	QSettings settings;
	int	aa = settings.value( "Settings/Render/General/Antialiasing", 4 ).toInt();
#ifdef Q_OS_LINUX
	// work around issues with MSAA > 4x on Linux
	if ( aa > 2 && !settings.value( "Settings/Render/General/Disable MSAA Limit", false ).toBool() ) {
		aa = 2;
		settings.setValue( "Settings/Render/General/Antialiasing", QVariant(aa) );
	}
#endif
	aa = std::min< int >( std::max< int >( aa, 0 ), 4 );

	// All new windows after the first window will share a format
	if ( share ) {
		fmt = share->format();
	} else {
		fmt.setSampleBuffers( bool(aa) );
	}

	// OpenGL version
	fmt.setVersion( 4, 0 );
	// Ignored if version < 3.2
	fmt.setProfile( QGLFormat::CompatibilityProfile );

	// V-Sync
	fmt.setSwapInterval( 1 );
	fmt.setDoubleBuffer( true );

	fmt.setSamples( 1 << aa );

	fmt.setDirectRendering( true );
	fmt.setRgba( true );

	views.append( QPointer<GLView>( new GLView( fmt, window, share ) ) );

	return views.last();
}

GLView::GLView( const QGLFormat & format, QWidget * p, const QGLWidget * shareWidget )
	: QGLWidget( format, p, shareWidget )
{
	setFocusPolicy( Qt::ClickFocus );
	//setAttribute( Qt::WA_PaintOnScreen );
	//setAttribute( Qt::WA_NoSystemBackground );
	setAutoFillBackground( false );
	setAcceptDrops( true );
	setContextMenuPolicy( Qt::CustomContextMenu );

	// Manually handle the buffer swap
	// Fixes bug with QGraphicsView and double buffering
	//	Input becomes sluggish and CPU usage doubles when putting GLView
	//	inside a QGraphicsView.
	setAutoBufferSwap( false );

	// Make the context current on this window
	makeCurrent();
	if ( !isValid() )
		return;

	// Create an OpenGL context
	glContext = context()->contextHandle();

	// Obtain a functions object and resolve all entry points
	glFuncs = glContext->functions();

	if ( !glFuncs ) {
		Message::critical( this, tr( "Could not obtain OpenGL functions" ) );
		exit( 1 );
	}

	glFuncs->initializeOpenGLFunctions();

	view = ViewDefault;
	animState = AnimEnabled;
	debugMode = DbgNone;

	Zoom = 1.0;

	doCenter  = false;
	doCompile = false;

	model = nullptr;

	time = 0.0;
	lastTime = QTime::currentTime();

	textures = new TexCache( this );

	updateSettings();

	scene = new Scene( textures, glContext, glFuncs );
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

	connect(NifSkope::getOptions(), &SettingsDialog::update3D, this, static_cast<void (GLView::*)()>(&GLView::updateSettings));
	connect(NifSkope::getOptions(), &SettingsDialog::update3D, [this]() {
		// Calling update() here in a lambda can crash..
		//updateSettings();
		qglClearColor(clearColor());
		//update();
	});
	connect(NifSkope::getOptions(), &SettingsDialog::update3D, this, static_cast<void (GLView::*)()>(&GLView::update));
}

GLView::~GLView()
{
	flush();

	delete textures;
	delete scene;
}

float	GLView::Settings::vertexPointSize = 5.0f;
float	GLView::Settings::tbnPointSize = 7.0f;
float	GLView::Settings::vertexSelectPointSize = 8.5f;
float	GLView::Settings::vertexPointSizeSelected = 10.0f;
float	GLView::Settings::lineWidthAxes = 2.0f;
float	GLView::Settings::lineWidthWireframe = 1.6f;
float	GLView::Settings::lineWidthHighlight = 2.5f;
float	GLView::Settings::lineWidthGrid1 = 1.0f;
float	GLView::Settings::lineWidthGrid2 = 0.25f;
float	GLView::Settings::lineWidthSelect = 5.0f;
float	GLView::Settings::zoomInScale = 0.95f;
float	GLView::Settings::zoomOutScale = 1.0f / 0.95f;

void GLView::updateSettings()
{
	QSettings settings;
	settings.beginGroup( "Settings/Render" );

	cfg.background = settings.value( "Colors/Background", QColor( 46, 46, 46 ) ).value<QColor>();
	cfg.fov = settings.value( "General/Camera/Field Of View" ).toFloat();
	cfg.moveSpd = settings.value( "General/Camera/Movement Speed" ).toFloat();
	cfg.rotSpd = settings.value( "General/Camera/Rotation Speed" ).toFloat();
	cfg.upAxis = UpAxis(settings.value( "General/Up Axis", ZAxis ).toInt());
	int	z = settings.value( "General/Camera/Startup Direction", 1 ).toInt();
	static const ViewState	startupDirections[6] = {
		ViewLeft, ViewFront, ViewTop, ViewRight, ViewBack, ViewBottom
	};
	cfg.startupDirection = startupDirections[std::min< int >( std::max< int >( z, 0 ), 5 )];
	z = settings.value( "General/Camera/Mwheel Zoom Speed", 8 ).toInt();
	z = std::min< int >( std::max< int >( z, 0 ), 16 );

	settings.endGroup();

	// TODO: make these configurable via the UI
	double	p = devicePixelRatioF();
	Settings::vertexPointSize = float( p * 5.0 );
	Settings::tbnPointSize = float( p * 7.0 );
	Settings::vertexSelectPointSize = float( p * 8.5 );
	Settings::vertexPointSizeSelected = float( p * 10.0 );
	Settings::lineWidthAxes = float( p * 2.0 );
	Settings::lineWidthWireframe = float( p * 1.6 );
	Settings::lineWidthHighlight = float( p * 2.5 );
	Settings::lineWidthGrid1 = float( p * 1.0 );
	Settings::lineWidthGrid2 = float( p * 0.25 );
	Settings::lineWidthSelect = float( p * 5.0 );

	double	tmp = std::pow( 0.95, std::sqrt( double(1 << z) * (1.0 / 256.0) ) );
	Settings::zoomInScale = float( tmp );
	Settings::zoomOutScale = float( 1.0 / tmp );
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
	QString	cfgPath( !isStarfield ? "Settings/Render/General/Cube Map Path FO 76" : "Settings/Render/General/Cube Map Path STF" );

	std::set< std::string_view >	fileSet;
	Game::GameManager::list_files( fileSet, (!isStarfield ? Game::FALLOUT_76 : Game::STARFIELD), &envMapFileListFilterFunction );
	QSettings	settings;
	std::string	prvPath( settings.value( cfgPath ).toString().toStdString() );
	if ( !prvPath.empty() && fileSet.find( prvPath ) == fileSet.end() )
		prvPath.clear();

	FileBrowserWidget	fileBrowser( 640, 480, "Select Default Environment Map", fileSet, prvPath );
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

QColor GLView::clearColor() const
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
		lastTime = QTime::currentTime();

		update();
	}
}


/*
 *  OpenGL
 */

void GLView::initializeGL()
{
	GLenum err;

	if ( scene->hasOption(Scene::DoMultisampling) ) {
		if ( !glContext->hasExtension( "GL_EXT_framebuffer_multisample" ) ) {
			scene->options &= ~Scene::DoMultisampling;
			//qDebug() << "System does not support multisampling";
		} /* else {
			GLint maxSamples;
			glGetIntegerv( GL_MAX_SAMPLES, &maxSamples );
			qDebug() << "Max samples:" << maxSamples;
		}*/
	}

	initializeTextureUnits( glContext );

	if ( scene->renderer->initialize() )
		updateShaders();

	// Initial viewport values
	//	Made viewport and aspect member variables.
	//	They were being updated every single frame instead of only when resizing.
	//glGetIntegerv( GL_VIEWPORT, viewport );
	aspect = (GLdouble)width() / (GLdouble)height();

	// Check for errors
	while ( ( err = glGetError() ) != GL_NO_ERROR )
		qDebug() << tr( "glview.cpp - GL ERROR (init) : " ) << getGLErrorString( int(err) );
}

void GLView::updateShaders()
{
	makeCurrent();
	if ( !isValid() )
		return;
	scene->updateShaders();
	update();
}

void GLView::glProjection( int x, int y )
{
	Q_UNUSED( x ); Q_UNUSED( y );

	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();

	BoundSphere bs = scene->view * scene->bounds();

	if ( scene->hasOption(Scene::ShowAxes) ) {
		bs |= BoundSphere( scene->view * Vector3(), axis );
	}

	float bounds = std::max< float >( bs.radius, 1024.0f * scale() );


	GLdouble nr = fabs( bs.center[2] ) - bounds * 1.5;
	GLdouble fr = fabs( bs.center[2] ) + bounds * 1.5;

	if ( perspectiveMode || (view == ViewWalk) ) {
		// Perspective View
		if ( nr > fr ) {
			// add: swap them when needed
			std::swap( nr, fr );
		}
		nr = std::max< GLdouble >( nr, scale() );
		// ensure distance
		fr = std::max< GLdouble >( fr, nr + scale() );

		GLdouble h2 = tan( ( cfg.fov / Zoom ) / 360 * M_PI ) * nr;
		GLdouble w2 = h2 * aspect;
		glFrustum( -w2, +w2, -h2, +h2, nr, fr );
	} else {
		// Orthographic View
		GLdouble h2 = Dist / Zoom;
		GLdouble w2 = h2 * aspect;
		glOrtho( -w2, +w2, -h2, +h2, nr, fr );
	}

	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
}


#ifdef USE_GL_QPAINTER
void GLView::paintEvent( QPaintEvent * event )
{
	makeCurrent();
	if ( !isValid() )
		return;

	QPainter painter;
	painter.begin( this );
	painter.setRenderHint( QPainter::TextAntialiasing );
#else
void GLView::paintGL()
{
#endif

	// Save GL state
	glPushAttrib( GL_ALL_ATTRIB_BITS );
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();

	// Clear Viewport
	if ( scene->hasVisMode(Scene::VisSilhouette) ) {
		glClearColor( 1.0f, 1.0f, 1.0f, 1.0f );
	}

	glDisable(GL_FRAMEBUFFER_SRGB);
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );


	// Compile the model
	if ( doCompile ) {
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
		doCompile = false;
	}

	// Center the model
	if ( doCenter ) {
		setCenter();
		doCenter = false;
	}

	// Transform the scene
	Matrix ap;

	if ( cfg.upAxis == YAxis ) {
		ap( 0, 0 ) = 0; ap( 0, 1 ) = 0; ap( 0, 2 ) = 1;
		ap( 1, 0 ) = 1; ap( 1, 1 ) = 0; ap( 1, 2 ) = 0;
		ap( 2, 0 ) = 0; ap( 2, 1 ) = 1; ap( 2, 2 ) = 0;
	} else if ( cfg.upAxis == XAxis ) {
		ap( 0, 0 ) = 0; ap( 0, 1 ) = 1; ap( 0, 2 ) = 0;
		ap( 1, 0 ) = 0; ap( 1, 1 ) = 0; ap( 1, 2 ) = 1;
		ap( 2, 0 ) = 1; ap( 2, 1 ) = 0; ap( 2, 2 ) = 0;
	}

	viewTrans.rotation.fromEuler( deg2rad(Rot[0]), deg2rad(Rot[1]), deg2rad(Rot[2]) );
	viewTrans.translation = viewTrans.rotation * Pos;
	viewTrans.rotation = viewTrans.rotation * ap;

	if ( view != ViewWalk )
		viewTrans.translation[2] -= Dist * 2;

	scene->transform( viewTrans, time );

	// Setup projection mode
	glProjection();
	glLoadIdentity();

	// Draw the grid
	if ( scene->hasOption(Scene::ShowGrid) ) {
		glDisable( GL_ALPHA_TEST );
		glDisable( GL_BLEND );
		glDisable( GL_LIGHTING );
		glDisable( GL_COLOR_MATERIAL );
		glEnable( GL_DEPTH_TEST );
		glDepthMask( GL_TRUE );
		glDepthFunc( GL_LESS );
		glDisable( GL_TEXTURE_2D );
		glDisable( GL_NORMALIZE );

		// Keep the grid "grounded" regardless of Up Axis
		Transform gridTrans = viewTrans;
		if ( cfg.upAxis != ZAxis )
			gridTrans.rotation = viewTrans.rotation * ap.inverted();

		glPushMatrix();
		glLoadMatrix( gridTrans );

		// TODO: Configurable grid in Settings
		// 1024 game units, major lines every 128, minor lines every 64
		drawGrid( 1024.0f * scale(), 16, 2 );

		glPopMatrix();
	}

#ifndef QT_NO_DEBUG
	// Debug scene bounds
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LESS );
	glPushMatrix();
	glLoadMatrix( viewTrans );
	if ( debugMode == DbgBounds ) {
		BoundSphere bs = scene->bounds();
		bs |= BoundSphere( Vector3(), axis );
		drawSphere( bs.center, bs.radius );
	}
	glPopMatrix();
#endif

	FloatVector4	mat_amb( 0.0f, 0.0f, 0.0f, 1.0f );
	FloatVector4	mat_diff( 0.0f, 0.0f, 0.0f, 1.0f );
	FloatVector4	mat_spec( 0.0f, 0.0f, 0.0f, 1.0f );

	if ( scene->hasVisMode(Scene::VisSilhouette) ) {
		if ( !scene->hasOption(Scene::DisableShaders) && scene->nifModel && scene->nifModel->getBSVersion() > 0 )
			mat_diff[3] = 0.0f;

		glShadeModel( GL_FLAT );
		//glEnable( GL_LIGHTING );
		glEnable( GL_LIGHT0 );
		glLightModelfv( GL_LIGHT_MODEL_AMBIENT, &(mat_diff[0]) );
		glMaterialfv( GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &(mat_diff[0]) );

	} else if ( scene->hasOption(Scene::DoLighting) ) {
		// Setup light
		Vector4 lightDir( 0.0, 0.0, 1.0, 0.0 );

		if ( !frontalLight ) {
			float decl = deg2rad( declination );
			Vector3 v( sin( decl ), 0, cos( decl ) );
			Matrix m; m.fromEuler( 0, 0, deg2rad( planarAngle ) );
			v = m * v;
			lightDir = Vector4( viewTrans.rotation * v, 0.0 );

			if ( scene->hasVisMode(Scene::VisLightPos) ) {
				glEnable( GL_BLEND );
				glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
				glEnable( GL_DEPTH_TEST );
				glDepthMask( GL_TRUE );
				glDepthFunc( GL_LESS );

				glPushMatrix();
				glLoadMatrix( viewTrans );

				glLineWidth( Settings::lineWidthAxes );
				glColor4f( 1.0f, 1.0f, 1.0f, 0.5f );

				// Scale the distance a bit
				float	s = scale() * 64.0f;
				float	l = axis + s;
				l = (l < s * 2.0f) ? axis * 1.5f : l;
				l = (l > s * 32.0f) ? axis * 0.66f : l;
				l = (l > s * 16.0f) ? axis * 0.75f : l;

				drawDashLine( Vector3( 0, 0, 0 ), v * l, 30 );
				drawSphere( v * l, axis / 10 );
				glPopMatrix();
				glDisable( GL_BLEND );
			}
		} else {
			// environment map rotation around the Z axis
			lightDir[3] = planarAngle / 180.0f;
		}

		float amb = ambient;
		if ( scene->hasOption(Scene::DisableShaders) )
			amb *= 0.375f;
		mat_amb = FloatVector4( amb );
		mat_amb[3] = toneMapping;

		const FloatVector4	a6( 0.02729229f, -0.03349948f, -0.93633725f, 0.0f );
		const FloatVector4	a5( 0.18128491f, -0.17835246f, 1.44207620f, 0.0f );
		const FloatVector4	a4( -0.31201837f, 0.31944694f, 0.89911656f, 0.0f );
		const FloatVector4	a3( -0.20153606f, 0.19871284f, -2.38214739f, 0.0f );
		const FloatVector4	a2( 0.68761390f, -0.68953811f, -0.04892082f, 0.0f );
		const FloatVector4	a1( -0.57557474f, 0.57604823f, 2.42514495f, 0.0f );
		const FloatVector4	a0( 1.0f );
		FloatVector4	c( lightColor );
		c = ( ( ( ( (c * a6 + a5) * c + a4 ) * c + a3 ) * c + a2 ) * c + a1 ) * c + a0;
		float	d1 = std::max( c[0], std::max(c[1], c[2]) );
		float	d0 = std::min( std::max(c[2], -0.039339175f), 0.0f );
		c = ( c - d0 ) / ( d1 - d0 );
		c.maxValues( FloatVector4(0.0f) ).minValues( FloatVector4(1.0f) );
		c *= brightnessL;
		mat_diff = c;
		mat_diff[3] = brightnessScale;
		mat_spec = mat_diff;


		glShadeModel( GL_SMOOTH );
		//glEnable( GL_LIGHTING );
		glEnable( GL_LIGHT0 );
		glLightfv( GL_LIGHT0, GL_POSITION, lightDir.data() );

	} else {
		float amb = scene->hasOption(Scene::DisableShaders) ? 0.0f : 0.5f;

		mat_amb.blendValues( FloatVector4( amb ), 0x07 );
		mat_diff = FloatVector4( 1.0f );


		glShadeModel( GL_SMOOTH );
		//glEnable( GL_LIGHTING );
		glEnable( GL_LIGHT0 );
	}

	glLightfv( GL_LIGHT0, GL_AMBIENT, &(mat_amb[0]) );
	glLightfv( GL_LIGHT0, GL_DIFFUSE, &(mat_diff[0]) );
	glLightfv( GL_LIGHT0, GL_SPECULAR, &(mat_spec[0]) );

	if ( scene->hasOption(Scene::DoMultisampling) )
		glEnable( GL_MULTISAMPLE_ARB );

#ifndef QT_NO_DEBUG
	// Color Key debug
	if ( debugMode == DbgColorPicker ) {
		glDisable( GL_MULTISAMPLE );
		glDisable( GL_LINE_SMOOTH );
		glDisable( GL_TEXTURE_2D );
		glDisable( GL_BLEND );
		glDisable( GL_DITHER );
		glDisable( GL_LIGHTING );
		glShadeModel( GL_FLAT );
		glDisable( GL_FOG );
		glDisable( GL_MULTISAMPLE_ARB );
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LEQUAL );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		Node::SELECTING = 1;
	} else {
		Node::SELECTING = 0;
	}
#endif

	// Draw the model
	scene->draw();

	if ( scene->hasOption(Scene::ShowAxes) ) {
		// Resize viewport to small corner of screen
		int axesSize = int( devicePixelRatioF() * 0.1 * std::min< int >( width(), 1250 ) + 0.5 );
		glViewport( 0, 0, axesSize, axesSize );

		// Reset matrices
		glMatrixMode( GL_PROJECTION );
		glLoadIdentity();

		// Square frustum
		auto nr = 1.0;
		auto fr = 250.0;
		GLdouble h2 = tan( cfg.fov / 360 * M_PI ) * nr;
		GLdouble w2 = h2;
		glFrustum( -w2, +w2, -h2, +h2, nr, fr );

		// Reset matrices
		glMatrixMode( GL_MODELVIEW );
		glLoadIdentity();

		glPushMatrix();

		// Store and reset viewTrans translation
		auto viewTransOrig = viewTrans.translation;

		// Zoom out slightly
		viewTrans.translation = { 0, 0, -150.0 };

		// Load modified viewTrans
		glLoadMatrix( viewTrans );

		// Restore original viewTrans translation
		viewTrans.translation = viewTransOrig;

		// Find direction of axes
		auto vtr = viewTrans.rotation;
		QVector<float> axesDots = { vtr( 2, 0 ), vtr( 2, 1 ), vtr( 2, 2 ) };

		drawAxesOverlay( { 0, 0, 0 }, 50.0, sortAxes( axesDots ) );

		glPopMatrix();

		// Restore viewport size
		QSize	sizeInPixels( getSizeInPixels() );
		glViewport( 0, 0, sizeInPixels.width(), sizeInPixels.height() );
		// Restore matrices
		glProjection();
	}

	// Restore GL state
	glPopAttrib();
	glMatrixMode( GL_MODELVIEW );
	glPopMatrix();
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();

	// Check for errors
	GLenum err;
	while ( ( err = glGetError() ) != GL_NO_ERROR )
		qDebug() << tr( "glview.cpp - GL ERROR (paint): " ) << getGLErrorString( int(err) );

	emit paintUpdate();

	// Manually handle the buffer swap
	swapBuffers();

#ifdef USE_GL_QPAINTER
	painter.end();
#endif
}


void GLView::resizeGL( int width, int height )
{
	double	p = 1.0 / devicePixelRatioF();
	resize( int( p * width + 0.5 ), int( p * height + 0.5 ) );

	makeCurrent();
	if ( !isValid() )
		return;
	aspect = GLdouble(width) / GLdouble(height);
	glViewport( 0, 0, width, height );

	glDisable(GL_FRAMEBUFFER_SRGB);
	qglClearColor(clearColor());
	update();
}

void GLView::resizeEvent( [[maybe_unused]] QResizeEvent * e )
{
	// This function should never be called.
	// Moved to NifSkope::eventFilter()
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

void GLView::setDeclination( int decl )
{
	declination = float(decl) / 4; // Divide by 4 because sliders are -720 <-> 720
	lightVisTimer->start( lightVisTimeout );
	setVisMode( Scene::VisLightPos, true );
	update();
}

void GLView::setPlanarAngle( int angle )
{
	planarAngle = float(angle) / 4; // Divide by 4 because sliders are -720 <-> 720
	lightVisTimer->start( lightVisTimeout );
	setVisMode( Scene::VisLightPos, true );
	update();
}

void GLView::setDebugMode( DebugMode mode )
{
	debugMode = mode;
}

void GLView::setVisMode( Scene::VisMode mode, bool checked )
{
	if ( checked )
		scene->visMode |= mode;
	else
		scene->visMode &= ~mode;

	update();
}

typedef void (Scene::* DrawFunc)( void );

int indexAt( /*GLuint *buffer,*/ NifModel * model, Scene * scene, QList<DrawFunc> drawFunc, int cycle, const QPointF & pos, int & furn )
{
	Q_UNUSED( model ); Q_UNUSED( cycle );
	// Color Key O(1) selection
	//	Open GL 3.0 says glRenderMode is deprecated
	//	ATI OpenGL API implementation of GL_SELECT corrupts NifSkope memory
	//
	// Create FBO for sharp edges and no shading.
	// Texturing, blending, dithering, lighting and smooth shading should be disabled.
	// The FBO can be used for the drawing operations to keep the drawing operations invisible to the user.

	GLint viewport[4];
	glGetIntegerv( GL_VIEWPORT, viewport );

	// Create new FBO with multisampling disabled
	QOpenGLFramebufferObjectFormat fboFmt;
	fboFmt.setTextureTarget( GL_TEXTURE_2D );
	fboFmt.setInternalTextureFormat( GL_RGBA8 );
	fboFmt.setAttachment( QOpenGLFramebufferObject::Attachment::CombinedDepthStencil );

	QOpenGLFramebufferObject fbo( viewport[2], viewport[3], fboFmt );
	fbo.bind();

	glDisable( GL_LIGHTING );
	glDisable( GL_MULTISAMPLE );
	glDisable( GL_MULTISAMPLE_ARB );
	glDisable( GL_LINE_SMOOTH );
	glDisable( GL_POINT_SMOOTH );
	glDisable( GL_POLYGON_SMOOTH );
	glDisable( GL_TEXTURE_1D );
	glDisable( GL_TEXTURE_2D );
	glDisable( GL_TEXTURE_3D );
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_BLEND );
	glDisable( GL_DITHER );
	glDisable( GL_FOG );
	glShadeModel( GL_FLAT );
	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	// Rasterize the scene
	Node::SELECTING = 1;
	for ( DrawFunc df : drawFunc ) {
		(scene->*df)();
	}
	Node::SELECTING = 0;

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
	if ( choose > 0 ) {
		auto furnBlock = model->getBlockIndex( model->index( 3, 0, model->getBlockIndex( choose & 0x0ffff ) ), "BSFurnitureMarker" );

		if ( furnBlock.isValid() ) {
			furn = choose >> 16;
			choose &= 0x0ffff;
		}
	}

	//qDebug() << "Key:" << a << " R" << pixel.red() << " G" << pixel.green() << " B" << pixel.blue();
	return choose;
}

QModelIndex GLView::indexAt( const QPointF & pos, int cycle )
{
	if ( !(model && isVisible() && height()) )
		return QModelIndex();

	makeCurrent();
	if ( !isValid() )
		return {};

	glPushAttrib( GL_ALL_ATTRIB_BITS );
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();

	double	p = devicePixelRatioF();
	int	wp = int( p * width() + 0.5 );
	int	hp = int( p * height() + 0.5 );
	QPointF	posScaled( pos );
	posScaled *= p;
	glViewport( 0, 0, wp, hp );
	glProjection( int( posScaled.x() + 0.5 ), int( posScaled.y() + 0.5 ) );

	QList<DrawFunc> df;

	if ( scene->hasOption(Scene::ShowCollision) )
		df << &Scene::drawHavok;

	if ( scene->hasOption(Scene::ShowNodes) )
		df << &Scene::drawNodes;

	if ( scene->hasOption(Scene::ShowMarkers) )
		df << &Scene::drawFurn;

	df << &Scene::drawShapes;

	int choose = -1, furn = -1;
	choose = ::indexAt( model, scene, df, cycle, posScaled, /*out*/ furn );

	glPopAttrib();
	glMatrixMode( GL_MODELVIEW );
	glPopMatrix();
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();

	QModelIndex chooseIndex;

	if ( scene->isSelModeVertex() ) {
		// Vertex
		int block = choose >> 16;
		int vert = choose & 0xFFFF;

		auto shape = scene->shapes.value( block );
		if ( shape )
			chooseIndex = shape->vertexAt( vert );
	} else if ( choose != -1 ) {
		// Block Index
		chooseIndex = model->getBlockIndex( choose );

		if ( furn != -1 ) {
			// Furniture Row @ Block Index
			chooseIndex = model->index( furn, 0, model->index( 3, 0, chooseIndex ) );
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
	Rot += Vector3( x, y, z );
	updateViewpoint();
	update();
}

void GLView::setCenter()
{
	Node * node = scene->getNode( model, scene->currentBlock );

	if ( node ) {
		// Center on selected node
		BoundSphere bs = node->bounds();

		this->setPosition( -bs.center );

		if ( bs.radius > 0 ) {
			setDistance( bs.radius * 1.2 );
		}
	} else {
		// Center on entire mesh
		BoundSphere bs = scene->bounds();

		if ( bs.radius < scale() )
			bs.radius = 1024.0 * scale();

		setDistance( bs.radius * 1.2 );
		setZoom( 1.0 );

		setPosition( -bs.center );

		setOrientation( view );
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
	{
		// TODO: Flip any other view also?
	}
		break;
	}

	setOrientation( tmp, false );
}

void GLView::setOrientation( GLView::ViewState state, bool recenter )
{
	if ( state == view )
		return;

	switch ( state ) {
	case ViewBottom:
		setRotation( 180, 0, 0 ); // Bottom
		break;
	case ViewTop:
		setRotation( 0, 0, 0 ); // Top
		break;
	case ViewBack:
		setRotation( -90, 0, 0 ); // Back
		break;
	case ViewFront:
		setRotation( -90, 0, 180 ); // Front
		break;
	case ViewRight:
		setRotation( -90, 0, 90 ); // Right
		break;
	case ViewLeft:
		setRotation( -90, 0, -90 ); // Left
		break;
	default:
		break;
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
	}

	doCompile = true;
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

	doCompile = true;
	//doCenter  = true;
	update();
}

void GLView::modelLinked()
{
	if ( doCompile )
		return;

	doCompile = true; //scene->update( model, QModelIndex() );
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

void GLView::advanceGears()
{
	QTime t  = QTime::currentTime();
	float dT = lastTime.msecsTo( t ) / 1000.0;
	dT = (dT < 0) ? 0 : ((dT > 1.0) ? 1.0 : dT);

	lastTime = t;

	if ( !isVisible() )
		return;

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

	// TODO: Some kind of input class for choosing the appropriate
	// keys based on user preferences of what app they would like to
	// emulate for the control scheme
	// Rotation
	if ( kbd[ Qt::Key_Up ] )    rotate( -cfg.rotSpd * dT, 0, 0 );
	if ( kbd[ Qt::Key_Down ] )  rotate( +cfg.rotSpd * dT, 0, 0 );
	if ( kbd[ Qt::Key_Left ] )  rotate( 0, 0, -cfg.rotSpd * dT );
	if ( kbd[ Qt::Key_Right ] ) rotate( 0, 0, +cfg.rotSpd * dT );

	// Fix movement speed for Starfield scale
	dT *= scale();
	// Movement
	if ( kbd[ Qt::Key_A ] ) move( +cfg.moveSpd * dT, 0, 0 );
	if ( kbd[ Qt::Key_D ] ) move( -cfg.moveSpd * dT, 0, 0 );
	if ( kbd[ Qt::Key_W ] ) move( 0, 0, +cfg.moveSpd * dT );
	if ( kbd[ Qt::Key_S ] ) move( 0, 0, -cfg.moveSpd * dT );
	if ( kbd[ Qt::Key_Q ] ) move( 0, +cfg.moveSpd * dT, 0 );
	if ( kbd[ Qt::Key_E ] ) move( 0, -cfg.moveSpd * dT, 0 );

	// Focal Length
	if ( kbd[ Qt::Key_PageUp ] )   setZoom( Zoom * std::sqrt( Settings::zoomOutScale ) );
	if ( kbd[ Qt::Key_PageDown ] ) setZoom( Zoom * std::sqrt( Settings::zoomInScale ) );

	if ( mouseMov[0] != 0 || mouseMov[1] != 0 || mouseMov[2] != 0 ) {
		move( mouseMov[0], mouseMov[1], mouseMov[2] );
		mouseMov = Vector3();
	}

	if ( mouseRot[0] != 0 || mouseRot[1] != 0 || mouseRot[2] != 0 ) {
		rotate( mouseRot[0], mouseRot[1], mouseRot[2] );
		mouseRot = Vector3();
	}

	// update display without movement
	if ( kbd[ Qt::Key_M ] ) update();
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
		imgFormat = std::min< int >( std::max< int >( imgFormat, 0 ), 4 );
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
			const QColor & c = cfg.background;
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
							glClearColor( c.redF(), c.greenF(), c.blueF(), 0.0f );
						scene->options = savedSceneOptions & ~( Scene::ShowAxes | Scene::ShowGrid );
						if ( i )
							scene->visMode = Scene::VisSilhouette;
					}
					update();
					updateGL();

					fbo.release();

					( i == 0 ? rgbImg : alphaImg ) = fbo.toImage();
				}
			} catch ( std::exception & e ) {
				err = e.what();
			}

			// Restore settings and return viewport to original size
			scene->options = savedSceneOptions;
			scene->visMode = savedSceneVisMode;
			glClearColor( c.redF(), c.greenF(), c.blueF(), c.alphaF() );
			if ( ss > 1 )
				resizeGL( int( p * w + 0.5 ), int( p * h + 0.5 ) );

			if ( !err.empty() ) {
				QMessageBox::critical( this, "NifSkope error", QString::fromStdString( err ) );
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
						throw FO76UtilsError( "%s", writer.errorString().toStdString().c_str() );

				} else {	// DDS
					DDSOutputFile	writer( file->file().toStdString().c_str(), imgWidth, imgHeight,
											DDSInputFile::pixelFormatRGBA32 );
					// TODO: portable handling of byte order
					writer.writeData( rgbImg.constBits(), size_t( rgbImg.sizeInBytes() ) );
				}

				dlg->accept();

			} catch ( std::exception & e ) {
				QMessageBox::critical( this, "NifSkope error", tr( "Could not save %1: %2" ).arg( file->file() ).arg( e.what() ) );
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

void GLView::dragEnterEvent( QDragEnterEvent * e )
{
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
	Q_UNUSED( e );

	if ( iDragTarget.isValid() ) {
		model->set<QString>( iDragTarget, fnDragTexOrg );
		iDragTarget = QModelIndex();
		fnDragTex = fnDragTexOrg = QString();
	}
}

void GLView::dragMoveEvent( QDragMoveEvent * e )
{
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
	iDragTarget = QModelIndex();
	fnDragTex = fnDragTexOrg = QString();
	e->accept();
}

void GLView::focusOutEvent( QFocusEvent * )
{
	kbd.clear();
}

void GLView::keyPressEvent( QKeyEvent * event )
{
	switch ( event->key() ) {
	case Qt::Key_Up:
	case Qt::Key_Down:
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_PageUp:
	case Qt::Key_PageDown:
	case Qt::Key_A:
	case Qt::Key_D:
	case Qt::Key_W:
	case Qt::Key_S:
	//case Qt::Key_R:
	//case Qt::Key_F:
	case Qt::Key_Q:
	case Qt::Key_E:
	case Qt::Key_M:
	case Qt::Key_Space:
		kbd[event->key()] = true;
		break;
	case Qt::Key_Escape:
		doCompile = true;

		if ( view == ViewWalk )
			doCenter = true;

		update();
		break;
	default:
		event->ignore();
		break;
	}
}

void GLView::keyReleaseEvent( QKeyEvent * event )
{
	switch ( event->key() ) {
	case Qt::Key_Up:
	case Qt::Key_Down:
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_PageUp:
	case Qt::Key_PageDown:
	case Qt::Key_A:
	case Qt::Key_D:
	case Qt::Key_W:
	case Qt::Key_S:
	//case Qt::Key_R:
	//case Qt::Key_F:
	case Qt::Key_Q:
	case Qt::Key_E:
	case Qt::Key_M:
	case Qt::Key_Space:
		kbd[event->key()] = false;
		break;
	default:
		event->ignore();
		break;
	}
}

void GLView::mouseDoubleClickEvent( QMouseEvent * )
{
	/*
	doCompile = true;
	if ( ! aViewWalk->isChecked() )
	doCenter = true;
	update();
	*/
}

void GLView::mouseMoveEvent( QMouseEvent * event )
{
	int dx = event->x() - lastPos.x();
	int dy = event->y() - lastPos.y();

	if ( event->buttons() & Qt::LeftButton && !kbd[Qt::Key_Space] ) {
		mouseRot += Vector3( dy * .5, 0, dx * .5 );
	} else if ( (event->buttons() & Qt::MiddleButton) || (event->buttons() & Qt::LeftButton && kbd[Qt::Key_Space]) ) {
		float d = axis / (qMax( width(), height() ) + 1);
		mouseMov += Vector3( dx * d, -dy * d, 0 );
	} else if ( event->buttons() & Qt::RightButton ) {
		setDistance( Dist - (dx + dy) * (axis / (qMax( width(), height() ) + 1)) );
	}

	lastPos = event->pos();
}

void GLView::mousePressEvent( QMouseEvent * event )
{
	if ( event->button() == Qt::ForwardButton || event->button() == Qt::BackButton ) {
		event->ignore();
		return;
	}

	lastPos = event->pos();

	if ( (pressPos - event->pos()).manhattanLength() <= 3 )
		cycleSelect++;
	else
		cycleSelect = 0;

	pressPos = event->pos();
}

void GLView::mouseReleaseEvent( QMouseEvent * event )
{
	if ( !(model && (pressPos - event->pos()).manhattanLength() <= 3) )
		return;

	if ( event->button() == Qt::ForwardButton || event->button() == Qt::BackButton || event->button() == Qt::MiddleButton ) {
		event->ignore();
		return;
	}

#ifdef Q_OS_LINUX
	bool	isColorPicker = bool( event->modifiers() & ( Qt::AltModifier | Qt::ControlModifier ) );
#else
	bool	isColorPicker = bool( event->modifiers() & Qt::AltModifier );
#endif
	if ( !isColorPicker ) {
		QModelIndex idx = indexAt( event->localPos(), cycleSelect );
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
		QOpenGLFramebufferObjectFormat fboFmt;
		fboFmt.setTextureTarget( GL_TEXTURE_2D );
		fboFmt.setInternalTextureFormat( GL_SRGB8 );
		fboFmt.setMipmap( false );
		fboFmt.setAttachment( QOpenGLFramebufferObject::Attachment::Depth );

		QSize	sizeInPixels( getSizeInPixels() );
		QOpenGLFramebufferObject fbo( sizeInPixels.width(), sizeInPixels.height(), fboFmt );
		fbo.bind();

		update();
		updateGL();

		fbo.release();

		QImage * img = new QImage( fbo.toImage() );

		auto what = img->pixel( ( event->localPos() * devicePixelRatioF() ).toPoint() );

		qglClearColor( QColor( what ) );
		// qDebug() << QColor( what );

		delete img;
	}

	update();
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


void GLGraphicsView::setupViewport( QWidget * viewport )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport);
	if ( glWidget ) {
		//glWidget->installEventFilter( this );
	}

	QGraphicsView::setupViewport( viewport );
}

bool GLGraphicsView::eventFilter( QObject * o, QEvent * e )
{
	//GLView * glWidget = qobject_cast<GLView *>(o);
	//if ( glWidget ) {
	//
	//}

	return QGraphicsView::eventFilter( o, e );
}

//void GLGraphicsView::paintEvent( QPaintEvent * e )
//{
//	GLView * glWidget = qobject_cast<GLView *>(viewport());
//	if ( glWidget ) {
//	//	glWidget->paintEvent( e );
//	}
//
//	QGraphicsView::paintEvent( e );
//}

void GLGraphicsView::drawForeground( QPainter * painter, const QRectF & rect )
{
	QGraphicsView::drawForeground( painter, rect );
}

void GLGraphicsView::drawBackground( QPainter * painter, const QRectF & rect )
{
	Q_UNUSED( painter ); Q_UNUSED( rect );

	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->updateGL();
	}

	//QGraphicsView::drawBackground( painter, rect );
}

void GLGraphicsView::dragEnterEvent( QDragEnterEvent * e )
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

	// Pass event on to viewport for any texture drag/drops
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->dragEnterEvent( e );
	}
}
void GLGraphicsView::dragLeaveEvent( QDragLeaveEvent * e )
{
	if ( !draggedNifs.isEmpty() ) {
		draggedNifs.clear();
		e->ignore();
		return;
	}

	// Pass event on to viewport for any texture drag/drops
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->dragLeaveEvent( e );
	}
}
void GLGraphicsView::dragMoveEvent( QDragMoveEvent * e )
{
	if ( !draggedNifs.isEmpty() ) {
		e->accept();
		return;
	}

	// Pass event on to viewport for any texture drag/drops
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->dragMoveEvent( e );
	}
}
void GLGraphicsView::dropEvent( QDropEvent * e )
{
	if ( !draggedNifs.isEmpty() ) {
		auto ns = qobject_cast<NifSkope *>(parentWidget());
		if ( ns ) {
			ns->openFiles( draggedNifs );
		}

		draggedNifs.clear();
		e->accept();
		return;
	}

	// Pass event on to viewport for any texture drag/drops
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->dropEvent( e );
	}
}
void GLGraphicsView::focusOutEvent( QFocusEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->focusOutEvent( e );
	}
}
void GLGraphicsView::keyPressEvent( QKeyEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->keyPressEvent( e );
	}
}
void GLGraphicsView::keyReleaseEvent( QKeyEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->keyReleaseEvent( e );
	}
}
void GLGraphicsView::mouseDoubleClickEvent( QMouseEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->mouseDoubleClickEvent( e );
	}
}
void GLGraphicsView::mouseMoveEvent( QMouseEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->mouseMoveEvent( e );
	}
}
void GLGraphicsView::mousePressEvent( QMouseEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->mousePressEvent( e );
	}
}
void GLGraphicsView::mouseReleaseEvent( QMouseEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->mouseReleaseEvent( e );
	}
}
void GLGraphicsView::wheelEvent( QWheelEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->wheelEvent( e );
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
