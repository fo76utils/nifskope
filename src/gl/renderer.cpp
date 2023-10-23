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

#include "renderer.h"

#include "message.h"
#include "nifskope.h"
#include "gl/glshape.h"
#include "gl/glproperty.h"
#include "gl/glscene.h"
#include "gl/gltex.h"
#include "io/material.h"
#include "model/nifmodel.h"
#include "ui/settingsdialog.h"
#include "gamemanager.h"
#include "gl/BSMesh.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSettings>
#include <QTextStream>


//! @file renderer.cpp Renderer and child classes implementation

bool shader_initialized = false;
bool shader_ready = true;

bool Renderer::initialize()
{
	if ( !shader_initialized ) {

		// check for OpenGL 2.0
		// (we don't use the extension API but the 2.0 API for shaders)
		if ( cfg.useShaders && fn->hasOpenGLFeature( QOpenGLFunctions::Shaders ) ) {
			shader_ready = true;
			shader_initialized = true;
		} else {
			shader_ready = false;
		}

		//qDebug() << "shader support" << shader_ready;
	}

	return shader_ready;
}

bool Renderer::hasShaderSupport()
{
	return shader_ready;
}

const QHash<Renderer::ConditionSingle::Type, QString> Renderer::ConditionSingle::compStrs{
	{ EQ,  " == " },
	{ NE,  " != " },
	{ LE,  " <= " },
	{ GE,  " >= " },
	{ LT,  " < " },
	{ GT,  " > " },
	{ AND, " & " },
	{ NAND, " !& " }
};

Renderer::ConditionSingle::ConditionSingle( const QString & line, bool neg ) : invert( neg )
{
	QHashIterator<Type, QString> i( compStrs );
	int pos = -1;

	while ( i.hasNext() ) {
		i.next();
		pos = line.indexOf( i.value() );

		if ( pos > 0 )
			break;
	}

	if ( pos > 0 ) {
		left  = line.left( pos ).trimmed();
		right = line.right( line.length() - pos - i.value().length() ).trimmed();

		if ( right.startsWith( "\"" ) && right.endsWith( "\"" ) )
			right = right.mid( 1, right.length() - 2 );

		comp = i.key();
	} else {
		left = line;
		comp = NONE;
	}
}

QModelIndex Renderer::ConditionSingle::getIndex( const NifModel * nif, const QVector<QModelIndex> & iBlocks, QString blkid ) const
{
	QString childid;

	if ( blkid.startsWith( "HEADER/" ) ) {
		auto blk = blkid.remove( "HEADER/" );
		if ( blk.contains("/") ) {
			auto blks = blk.split( "/" );
			return nif->getIndex( nif->getIndex( nif->getHeaderIndex(), blks.at(0) ), blks.at(1) );
		}
		return nif->getIndex( nif->getHeaderIndex(), blk );
	}

	int pos = blkid.indexOf( "/" );

	if ( pos > 0 ) {
		childid = blkid.right( blkid.length() - pos - 1 );
		blkid = blkid.left( pos );
	}

	for ( QModelIndex iBlock : iBlocks ) {
		if ( nif->blockInherits( iBlock, blkid ) ) {
			if ( childid.isEmpty() )
				return iBlock;

			return nif->getIndex( iBlock, childid );
		}
	}
	return QModelIndex();
}

bool Renderer::ConditionSingle::eval( const NifModel * nif, const QVector<QModelIndex> & iBlocks ) const
{
	QModelIndex iLeft = getIndex( nif, iBlocks, left );

	if ( !iLeft.isValid() )
		return invert;

	if ( comp == NONE )
		return !invert;

	const NifItem * item = nif->getItem( iLeft );
	if ( !item )
		return false;

	if ( item->isString() )
		return compare( item->getValueAsString(), right ) ^ invert;
	else if ( item->isCount() )
		return compare( item->getCountValue(), right.toULongLong( nullptr, 0 ) ) ^ invert;
	else if ( item->isFloat() )
		return compare( item->getFloatValue(), (float)right.toDouble() ) ^ invert;
	else if ( item->isFileVersion() )
		return compare( item->getFileVersionValue(), right.toUInt( nullptr, 0 ) ) ^ invert;
	else if ( item->valueType() == NifValue::tBSVertexDesc )
		return compare( (uint) item->get<BSVertexDesc>().GetFlags(), right.toUInt( nullptr, 0 ) ) ^ invert;

	return false;
}

bool Renderer::ConditionGroup::eval( const NifModel * nif, const QVector<QModelIndex> & iBlocks ) const
{
	if ( conditions.isEmpty() )
		return true;

	if ( isOrGroup() ) {
		for ( Condition * cond : conditions ) {
			if ( cond->eval( nif, iBlocks ) )
				return true;
		}
		return false;
	} else {
		for ( Condition * cond : conditions ) {
			if ( !cond->eval( nif, iBlocks ) )
				return false;
		}
		return true;
	}
}

void Renderer::ConditionGroup::addCondition( Condition * c )
{
	conditions.append( c );
}

Renderer::Shader::Shader( const QString & n, GLenum t, QOpenGLFunctions * fn )
	: f( fn ), name( n ), id( 0 ), status( false ), type( t )
{
	id = f->glCreateShader( type );
}

Renderer::Shader::~Shader()
{
	if ( id )
		f->glDeleteShader( id );
}

bool Renderer::Shader::load( const QString & filepath )
{
	try
	{
		QFile file( filepath );

		if ( !file.open( QIODevice::ReadOnly ) )
			throw QString( "couldn't open %1 for read access" ).arg( filepath );

		QByteArray data = file.readAll();

		const char * src = data.constData();

		f->glShaderSource( id, 1, &src, 0 );
		f->glCompileShader( id );

		GLint result;
		f->glGetShaderiv( id, GL_COMPILE_STATUS, &result );

		if ( result != GL_TRUE ) {
			GLint logLen;
			f->glGetShaderiv( id, GL_INFO_LOG_LENGTH, &logLen );
			char * log = new char[ logLen ];
			f->glGetShaderInfoLog( id, logLen, 0, log );
			QString errlog( log );
			delete[] log;
			throw errlog;
		}
	}
	catch ( QString & err )
	{
		status = false;
		Message::append( QObject::tr( "There were errors during shader compilation" ), QString( "%1:\r\n\r\n%2" ).arg( name ).arg( err ) );
		return false;
	}
	status = true;
	return true;
}


Renderer::Program::Program( const QString & n, QOpenGLFunctions * fn )
	: f( fn ), name( n ), id( 0 )
{
	uniformLocationsSF.resize( 2048, std::pair< std::uint32_t, int >(0, -1) );
	id = f->glCreateProgram();
}

Renderer::Program::~Program()
{
	if ( id )
		f->glDeleteShader( id );
}

bool Renderer::Program::load( const QString & filepath, Renderer * renderer )
{
	try
	{
		QFile file( filepath );

		if ( !file.open( QIODevice::ReadOnly ) )
			throw QString( "couldn't open %1 for read access" ).arg( filepath );

		QTextStream stream( &file );

		QStack<ConditionGroup *> chkgrps;
		chkgrps.push( &conditions );

		while ( !stream.atEnd() ) {
			QString line = stream.readLine().trimmed();

			if ( line.startsWith( "shaders" ) ) {
				QStringList list = line.simplified().split( " " );

				for ( int i = 1; i < list.count(); i++ ) {
					Shader * shader = renderer->shaders.value( list[ i ] );

					if ( shader ) {
						if ( shader->status )
							f->glAttachShader( id, shader->id );
						else
							throw QString( "depends on shader %1 which was not compiled successful" ).arg( list[ i ] );
					} else {
						throw QString( "shader %1 not found" ).arg( list[ i ] );
					}
				}
			} else if ( line.startsWith( "checkgroup" ) ) {
				QStringList list = line.simplified().split( " " );

				if ( list.value( 1 ) == "begin" ) {
					ConditionGroup * group = new ConditionGroup( list.value( 2 ) == "or" );
					chkgrps.top()->addCondition( group );
					chkgrps.push( group );
				} else if ( list.value( 1 ) == "end" ) {
					if ( chkgrps.count() > 1 )
						chkgrps.pop();
					else
						throw QString( "mismatching checkgroup end tag" );
				} else {
					throw QString( "expected begin or end after checkgroup" );
				}
			} else if ( line.startsWith( "check" ) ) {
				line = line.remove( 0, 5 ).trimmed();

				bool invert = false;

				if ( line.startsWith( "not " ) ) {
					invert = true;
					line = line.remove( 0, 4 ).trimmed();
				}

				chkgrps.top()->addCondition( new ConditionSingle( line, invert ) );
			} else if ( line.startsWith( "texcoords" ) ) {
				line = line.remove( 0, 9 ).simplified();
				QStringList list = line.split( " " );
				bool ok;
				int unit = list.value( 0 ).toInt( &ok );
				QString idStr = list.value( 1 ).toLower();

				if ( !ok || idStr.isEmpty() )
					throw QString( "malformed texcoord tag" );

				int id = -1;
				if ( idStr == "tangents" )
					id = CT_TANGENT;
				else if ( idStr == "bitangents" )
					id = CT_BITANGENT;
				else if ( idStr == "indices" )
					id = CT_BONE;
				else if ( idStr == "weights" )
					id = CT_WEIGHT;
				else if ( idStr == "base" )
					id = TexturingProperty::getId( idStr );

				if ( id < 0 )
					throw QString( "texcoord tag refers to unknown texture id '%1'" ).arg( idStr );

				if ( texcoords.contains( unit ) )
					throw QString( "texture unit %1 is assigned twiced" ).arg( unit );

				texcoords.insert( unit, CoordType(id) );
			}
		}

		f->glLinkProgram( id );

		GLint result;

		f->glGetProgramiv( id, GL_LINK_STATUS, &result );

		if ( result != GL_TRUE ) {
			GLint logLen = 0;
			f->glGetProgramiv( id, GL_INFO_LOG_LENGTH, &logLen );

			if ( logLen != 0 ) {
				char * log = new char[ logLen ];
				f->glGetProgramInfoLog( id, logLen, 0, log );
				QString errlog( log );
				delete[] log;
				id = 0;
				throw errlog;
			}
		}
	}
	catch ( QString & x )
	{
		status = false;
		Message::append( QObject::tr( "There were errors during shader compilation" ), QString( "%1:\r\n\r\n%2" ).arg( name ).arg( x ) );
		return false;
	}
	status = true;
	return true;
}

void Renderer::Program::setUniformLocations()
{
	for ( int i = 0; i < NUM_UNIFORM_TYPES; i++ )
		uniformLocations[i] = f->glGetUniformLocation( id, uniforms[i].c_str() );
}

Renderer::Renderer( QOpenGLContext * c, QOpenGLFunctions * f )
	: cx( c ), fn( f )
{
	updateSettings();

	connect( NifSkope::getOptions(), &SettingsDialog::saveSettings, this, &Renderer::updateSettings );
}

Renderer::~Renderer()
{
	releaseShaders();
}


void Renderer::updateSettings()
{
	QSettings settings;

	cfg.useShaders = settings.value( "Settings/Render/General/Use Shaders", true ).toBool();

	bool prevStatus = shader_ready;

	shader_ready = cfg.useShaders && fn->hasOpenGLFeature( QOpenGLFunctions::Shaders );
	if ( !shader_initialized && shader_ready && !prevStatus ) {
		updateShaders();
		shader_initialized = true;
	}
}

void Renderer::updateShaders()
{
	if ( !shader_ready )
		return;

	releaseShaders();

	QDir dir( QCoreApplication::applicationDirPath() );

	if ( dir.exists( "shaders" ) )
		dir.cd( "shaders" );

#ifdef Q_OS_LINUX
	else if ( dir.exists( "/usr/share/nifskope/shaders" ) )
		dir.cd( "/usr/share/nifskope/shaders" );
#endif

	dir.setNameFilters( { "*.vert" } );
	for ( const QString& name : dir.entryList() ) {
		Shader * shader = new Shader( name, GL_VERTEX_SHADER, fn );
		shader->load( dir.filePath( name ) );
		shaders.insert( name, shader );
	}

	dir.setNameFilters( { "*.frag" } );
	for ( const QString& name : dir.entryList() ) {
		Shader * shader = new Shader( name, GL_FRAGMENT_SHADER, fn );
		shader->load( dir.filePath( name ) );
		shaders.insert( name, shader );
	}

	dir.setNameFilters( { "*.prog" } );
	for ( const QString& name : dir.entryList() ) {
		Program * program = new Program( name, fn );
		program->load( dir.filePath( name ), this );
		program->setUniformLocations();
		programs.insert( name, program );
	}
}

void Renderer::releaseShaders()
{
	if ( !shader_ready )
		return;

	qDeleteAll( programs );
	programs.clear();
	qDeleteAll( shaders );
	shaders.clear();
}

QString Renderer::setupProgram( Shape * mesh, const QString & hint )
{
	PropertyList props;
	mesh->activeProperties( props );

	auto nif = NifModel::fromValidIndex(mesh->index());
	if ( !shader_ready
			|| hint.isNull()
			|| mesh->scene->hasOption(Scene::DisableShaders)
			|| mesh->scene->hasVisMode(Scene::VisSilhouette)
			|| !nif
			|| (nif->getBSVersion() == 0)
	) {
		setupFixedFunction( mesh, props );
		return {};
	}

	QVector<QModelIndex> iBlocks;
	iBlocks << mesh->index();
	iBlocks << mesh->iData;
	for ( Property * p : props.list() ) {
		iBlocks.append( p->index() );
	}

	if ( !hint.isEmpty() ) {
		Program * program = programs.value( hint );
		if ( program && program->status && setupProgram( program, mesh, props, iBlocks, false ) )
			return program->name;
	}

	for ( Program * program : programs ) {
		if ( program->status && setupProgram( program, mesh, props, iBlocks ) )
			return program->name;
	}

	stopProgram();
	setupFixedFunction( mesh, props );
	return {};
}

void Renderer::stopProgram()
{
	if ( shader_ready ) {
		fn->glUseProgram( 0 );
	}

	resetTextureUnits();
}

void Renderer::Program::uni1f( UniformType var, float x )
{
	f->glUniform1f( uniformLocations[var], x );
}

void Renderer::Program::uni2f( UniformType var, float x, float y )
{
	f->glUniform2f( uniformLocations[var], x, y );
}

void Renderer::Program::uni3f( UniformType var, float x, float y, float z )
{
	f->glUniform3f( uniformLocations[var], x, y, z );
}

void Renderer::Program::uni4f( UniformType var, float x, float y, float z, float w )
{
	f->glUniform4f( uniformLocations[var], x, y, z, w );
}

void Renderer::Program::uni1i( UniformType var, int val )
{
	f->glUniform1i( uniformLocations[var], val );
}

void Renderer::Program::uni3m( UniformType var, const Matrix & val )
{
	if ( uniformLocations[var] >= 0 )
		f->glUniformMatrix3fv( uniformLocations[var], 1, 0, val.data() );
}

void Renderer::Program::uni4m( UniformType var, const Matrix4 & val )
{
	if ( uniformLocations[var] >= 0 )
		f->glUniformMatrix4fv( uniformLocations[var], 1, 0, val.data() );
}

bool Renderer::Program::uniSampler( BSShaderLightingProperty * bsprop, UniformType var,
									int textureSlot, int & texunit, const QString & alternate,
									uint clamp, const QString & forced )
{
	GLint uniSamp = uniformLocations[var];
	if ( uniSamp >= 0 ) {

		// TODO: On stream 155 bsprop->fileName can reference incorrect strings because
		// the BSSTS is not filled out nor linked from the BSSP
		QString fname = (forced.isEmpty()) ? bsprop->fileName( textureSlot ) : forced;
		if ( fname.isEmpty() )
			fname = alternate;

		if ( !fname.isEmpty() && (!activateTextureUnit( texunit )
										|| !(bsprop->bind( textureSlot, fname, TexClampMode(clamp) )
										|| bsprop->bind( textureSlot, alternate, TexClampMode(3) ))) )
			return uniSamplerBlank( var, texunit );

		f->glUniform1i( uniSamp, texunit++ );

		return true;
	}

	return true;
}

bool Renderer::Program::uniSamplerBlank( UniformType var, int & texunit )
{
	GLint uniSamp = uniformLocations[var];
	if ( uniSamp >= 0 ) {
		if ( !activateTextureUnit( texunit ) )
			return false;

		glBindTexture( GL_TEXTURE_2D, 0 );
		f->glUniform1i( uniSamp, texunit++ );

		return true;
	}

	return true;
}

#ifdef __GNUC__
__attribute__ ((__format__ (__printf__, 2, 3)))
#endif
int Renderer::Program::uniLocation( const char * fmt, ... )
{
	char	varNameBuf[256];
	std::va_list	ap;
	va_start(ap, fmt);
	char	*sp = varNameBuf;
	char	*endp = sp + 255;
	std::uint32_t	h = 0;
	for ( ; *fmt && sp < endp; fmt++, sp++ ) {
		char	c = *fmt;
		if ( c == '%' ) {
			fmt++;
			c = *fmt;
			if ( c == 'd' ) {
				int	n = va_arg(ap, int);
				if ( n >= 10 && (sp + 1) < endp ) {
					*sp = char((n / 10) & 15) | '0';
					hashFunctionCRC32C< unsigned char >(h, (unsigned char) *sp);
					sp++;
					n = n % 10;
				}
				c = char(n & 15) | '0';
			} else if ( c != '%' ) {
				break;
			}
		}
		*sp = c;
		hashFunctionCRC32C< unsigned char >(h, (unsigned char) *sp);
	}
	va_end(ap);
	*sp = '\0';
	size_t	hashMask = uniformLocationsSF.size() - 1;
	size_t	i = h & hashMask;
	for ( ; uniformLocationsSF[i].first; i = (i + 1) & hashMask) {
		if ( uniformLocationsSF[i].first == h )
			return uniformLocationsSF[i].second;
	}
	int	l = f->glGetUniformLocation( id, varNameBuf );
	uniformLocationsSF[i] = std::pair< std::uint32_t, int >(h, l);
#if 0
	std::fprintf(stderr, "Hash 0x%08X: uniform '%s' location %d\n", (unsigned int) h, varNameBuf, l);
#endif
	if ( l < 0 )
		qWarning() << "Warning: uniform '" << varNameBuf << "' not found";
	return l;
}

void Renderer::Program::uni1b_l( int l, bool x )
{
	f->glUniform1i( l, int(x) );
}

void Renderer::Program::uni1i_l( int l, int x )
{
	f->glUniform1i( l, x );
}

void Renderer::Program::uni1f_l( int l, float x )
{
	f->glUniform1f( l, x );
}

void Renderer::Program::uni2f_l( int l, float x, float y )
{
	f->glUniform2f( l, x, y );
}

void Renderer::Program::uni4f_l( int l, FloatVector4 x, bool isSRGB )
{
	if ( isSRGB ) {
		float	a = x[3];
		x *= (x * 0.13945550f + 0.86054450f);
		x *= x;
		x[3] = a;
	}
	f->glUniform4f( l, x[0], x[1], x[2], x[3] );
}

void Renderer::Program::uni4c_l( int l, std::uint32_t c, bool isSRGB)
{
	FloatVector4	x(c);
	if ( !isSRGB ) {
		x *= 1.0f / 255.0f;
	} else {
		float	a = x[3] * (1.0f / 255.0f);
		x.srgbExpand();
		x[3] = a;
	}
	f->glUniform4f( l, x[0], x[1], x[2], x[3] );
}

bool Renderer::Program::uniSampler_l( BSShaderLightingProperty * bsprop, int & texunit, int l1, int l2, const std::string * texturePath, std::uint32_t textureReplacement, int textureReplacementMode, const CE2Material::UVStream * uvStream )
{
	if ( l1 < 0 )
		return false;
	FloatVector4	c(textureReplacement);
	if ( textureReplacementMode < 2 )
		c *= 1.0f / 255.0f;	// UNORM
	else if ( textureReplacementMode == 2 )
		c.srgbExpand();	// SRGB
	else
		c = c * (1.0f / 127.5f) - 1.0f;	// SNORM
	if ( texturePath && !texturePath->empty() && texunit >= 1 && texunit <= 15 && activateTextureUnit(texunit) ) {
		TexClampMode	clampMode = TexClampMode::WRAP_S_WRAP_T;
		if ( uvStream && uvStream->textureAddressMode ) {
			if ( uvStream->textureAddressMode == 3 ) {
				// this may be incorrect
				glTexParameterfv( GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &(c.v[0]) );
				clampMode = TexClampMode::BORDER_S_BORDER_T;
			} else if ( uvStream->textureAddressMode == 2 )
				clampMode = TexClampMode::MIRRORED_S_MIRRORED_T;
			else
				clampMode = TexClampMode::CLAMP_S_CLAMP_T;
		}
		if ( bsprop->bind( -1, QString::fromStdString(*texturePath), clampMode ) ) {
			f->glUniform1i( uniLocation("textureUnits[%d]", texunit - 1), texunit );
			f->glUniform1i( l1, texunit++ );
			return true;
		}
	}
	if ( textureReplacementMode > 0 && l2 >= 0 ) {
		f->glUniform1i( l1, -1 );
		f->glUniform4f( l2, c[0], c[1], c[2], c[3] );
		return true;
	}
	f->glUniform1i( l1, 0 );
	return false;
}

static QString white = "#FFFFFFFF";
static QString black = "#FF000000";
static QString lighting = "#FF00F040";
static QString reflectivity = "#FF0A0A0A";
static QString gray = "#FF808080s";
static QString magenta = "#FFFF00FF";
static QString default_n = "#FFFF8080";
static QString default_ns = "#FFFF8080n";
static QString cube_sk = "textures/cubemaps/bleakfallscube_e.dds";
static QString cube_fo4_76 = "textures/shared/cubemaps/mipblur_defaultoutside1.dds";
static QString cube_sf = "textures/cubemaps/cell_cityplazacube.dds";

static const std::uint32_t defaultSFTextureSet[21] = {
	0xFFFF00FFU, 0xFFFF8080U, 0xFFFFFFFFU, 0xFFC0C0C0U, 0xFF000000U, 0xFFFFFFFFU,
	0xFF000000U, 0xFF000000U, 0xFF000000U, 0xFF808080U, 0xFF000000U, 0xFF808080U,
	0xFF000000U, 0xFF000000U, 0xFF808080U, 0xFF808080U, 0xFF808080U, 0xFF000000U,
	0xFF000000U, 0xFFFFFFFFU, 0xFF808080U
};

bool Renderer::setupProgramSF( Program * prog, Shape * mesh )
{
	auto nif = NifModel::fromValidIndex( mesh->index() );
	if ( !nif )
		return false;

	fn->glUseProgram( prog->id );

	auto scene = mesh->scene;
	auto lsp = mesh->bslsp;
	if ( !lsp )
		return false;

	const CE2Material *	mat = lsp->getSFMaterial();
	bool	useErrorColor = false;
	if ( !mat ) {
		const CE2MaterialDB *	matDB = Game::GameManager::materials( Game::STARFIELD );
		if ( !matDB )
			return false;
		if ( lsp )
			mat = matDB->findMaterial( nif->get<QString>( lsp->index(), "Name" ).toStdString() );
		if ( !mat ) {
			mat = matDB->findMaterial( std::string("materials/test/generic/test_generic_white.mat") );
			useErrorColor = scene->hasOption(Scene::DoErrorColor);
		}
	}
	if ( !mat )
		return false;

	mesh->depthWrite = true;
	mesh->depthTest = true;
	if ( mat->flags & CE2Material::Flag_IsEffect ) {
		mesh->depthWrite = bool(mat->effectSettings->flags & CE2Material::EffectFlag_ZWrite);
		mesh->depthTest = bool(mat->effectSettings->flags & CE2Material::EffectFlag_ZTest);
	}

	// texturing

	BSShaderLightingProperty * bsprop = mesh->bssp;
	if ( !bsprop )
		return false;
	// BSShaderLightingProperty * bsprop = props.get<BSShaderLightingProperty>();
	// TODO: BSLSP has been split off from BSShaderLightingProperty so it needs
	//	to be accessible from here

	int texunit = 0;

	// Always bind cube to texture unit 0, regardless of shader settings
	prog->uni1i( HAS_MAP_CUBE, scene->hasOption(Scene::DoCubeMapping) && scene->hasOption(Scene::DoLighting) );
	GLint uniCubeMap = prog->uniformLocations[SAMP_CUBE];
	if ( uniCubeMap >= 0 ) {
		QString	fname = cube_sf;

		if ( !activateTextureUnit( texunit ) || !bsprop->bindCube( fname ) )
			return false;

		fn->glUniform1i( uniCubeMap, texunit++ );
	}

	prog->uni1b_l( prog->uniLocation("isWireframe"), false );
	prog->uni1f_l( prog->uniLocation("envReflection"), 0.000104f );
	prog->uni1i_l( prog->uniLocation("lm.shaderModel"), mat->shaderModel );
	prog->uni1b_l( prog->uniLocation("lm.isEffect"), bool(mat->flags & CE2Material::Flag_IsEffect) );
	prog->uni1b_l( prog->uniLocation("lm.isTwoSided"), bool(mat->flags & CE2Material::Flag_TwoSided) );
	prog->uni1b_l( prog->uniLocation("lm.hasOpacityComponent"), bool(mat->flags & CE2Material::Flag_HasOpacityComponent) );
	if ( mat->flags & CE2Material::Flag_LayeredEmissivity ) {
		prog->uni1b_l( prog->uniLocation("lm.layeredEmissivity.isEnabled"), mat->layeredEmissiveSettings->isEnabled );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.firstLayerIndex"), mat->layeredEmissiveSettings->layer1Index );
		prog->uni4c_l( prog->uniLocation("lm.layeredEmissivity.firstLayerTint"), mat->layeredEmissiveSettings->layer1Tint );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.firstLayerMaskIndex"), mat->layeredEmissiveSettings->layer1MaskIndex );
		prog->uni1b_l( prog->uniLocation("lm.layeredEmissivity.secondLayerActive"), mat->layeredEmissiveSettings->layer2Active );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.secondLayerIndex"), mat->layeredEmissiveSettings->layer2Index );
		prog->uni4c_l( prog->uniLocation("lm.layeredEmissivity.secondLayerTint"), mat->layeredEmissiveSettings->layer2Tint );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.secondLayerMaskIndex"), mat->layeredEmissiveSettings->layer2MaskIndex );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.firstBlenderIndex"), mat->layeredEmissiveSettings->blender1Index );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.firstBlenderMode"), mat->layeredEmissiveSettings->blender1Mode );
		prog->uni1b_l( prog->uniLocation("lm.layeredEmissivity.thirdLayerActive"), mat->layeredEmissiveSettings->layer3Active );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.thirdLayerIndex"), mat->layeredEmissiveSettings->layer3Index );
		prog->uni4c_l( prog->uniLocation("lm.layeredEmissivity.thirdLayerTint"), mat->layeredEmissiveSettings->layer3Tint );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.thirdLayerMaskIndex"), mat->layeredEmissiveSettings->layer3MaskIndex );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.secondBlenderIndex"), mat->layeredEmissiveSettings->blender2Index );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.secondBlenderMode"), mat->layeredEmissiveSettings->blender2Mode );
		prog->uni1f_l( prog->uniLocation("lm.layeredEmissivity.emissiveClipThreshold"), mat->layeredEmissiveSettings->clipThreshold );
		prog->uni1b_l( prog->uniLocation("lm.layeredEmissivity.adaptiveEmittance"), mat->layeredEmissiveSettings->adaptiveEmittance );
		prog->uni1f_l( prog->uniLocation("lm.layeredEmissivity.luminousEmittance"), mat->layeredEmissiveSettings->luminousEmittance );
		prog->uni1f_l( prog->uniLocation("lm.layeredEmissivity.exposureOffset"), mat->layeredEmissiveSettings->exposureOffset );
		prog->uni1b_l( prog->uniLocation("lm.layeredEmissivity.enableAdaptiveLimits"), mat->layeredEmissiveSettings->enableAdaptiveLimits );
		prog->uni1f_l( prog->uniLocation("lm.layeredEmissivity.maxOffsetEmittance"), mat->layeredEmissiveSettings->maxOffset );
		prog->uni1f_l( prog->uniLocation("lm.layeredEmissivity.minOffsetEmittance"), mat->layeredEmissiveSettings->minOffset );
	}	else {
		prog->uni1b_l( prog->uniLocation("lm.layeredEmissivity.isEnabled"), false );
	}
	if ( mat->flags & CE2Material::Flag_Emissive ) {
		prog->uni1b_l( prog->uniLocation("lm.emissiveSettings.isEnabled"), mat->emissiveSettings->isEnabled );
		prog->uni1i_l( prog->uniLocation("lm.emissiveSettings.emissiveSourceLayer"), mat->emissiveSettings->sourceLayer );
		prog->uni4f_l( prog->uniLocation("lm.emissiveSettings.emissiveTint"), mat->emissiveSettings->emissiveTint );
		prog->uni1i_l( prog->uniLocation("lm.emissiveSettings.emissiveMaskSourceBlender"), mat->emissiveSettings->maskSourceBlender );
		prog->uni1f_l( prog->uniLocation("lm.emissiveSettings.emissiveClipThreshold"), mat->emissiveSettings->clipThreshold );
		prog->uni1b_l( prog->uniLocation("lm.emissiveSettings.adaptiveEmittance"), mat->emissiveSettings->adaptiveEmittance );
		prog->uni1f_l( prog->uniLocation("lm.emissiveSettings.luminousEmittance"), mat->emissiveSettings->luminousEmittance );
		prog->uni1f_l( prog->uniLocation("lm.emissiveSettings.exposureOffset"), mat->emissiveSettings->exposureOffset );
		prog->uni1b_l( prog->uniLocation("lm.emissiveSettings.enableAdaptiveLimits"), mat->emissiveSettings->enableAdaptiveLimits );
		prog->uni1f_l( prog->uniLocation("lm.emissiveSettings.maxOffsetEmittance"), mat->emissiveSettings->maxOffset );
		prog->uni1f_l( prog->uniLocation("lm.emissiveSettings.minOffsetEmittance"), mat->emissiveSettings->minOffset );
	}	else {
		prog->uni1b_l( prog->uniLocation("lm.emissiveSettings.isEnabled"), false );
	}
	if ( mat->flags & CE2Material::Flag_IsDecal ) {
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.isDecal"), mat->decalSettings->isDecal );
		prog->uni1f_l( prog->uniLocation("lm.decalSettings.materialOverallAlpha"), mat->decalSettings->decalAlpha );
		prog->uni1i_l( prog->uniLocation("lm.decalSettings.writeMask"), int(mat->decalSettings->writeMask) );
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.isPlanet"), mat->decalSettings->isPlanet );
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.isProjected"), mat->decalSettings->isProjected );
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.useParallaxOcclusionMapping"), mat->decalSettings->useParallaxMapping );
		prog->uniSampler_l( bsprop, texunit, prog->uniLocation("lm.decalSettings.surfaceHeightMap"), -1, mat->decalSettings->surfaceHeightMap, 0, 0, nullptr );
		prog->uni1f_l( prog->uniLocation("lm.decalSettings.parallaxOcclusionScale"), mat->decalSettings->parallaxOcclusionScale );
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.parallaxOcclusionShadows"), mat->decalSettings->parallaxOcclusionShadows );
		prog->uni1i_l( prog->uniLocation("lm.decalSettings.maxParralaxOcclusionSteps"), mat->decalSettings->maxParallaxSteps );
		prog->uni1i_l( prog->uniLocation("lm.decalSettings.renderLayer"), mat->decalSettings->renderLayer );
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.useGBufferNormals"), mat->decalSettings->useGBufferNormals );
		prog->uni1i_l( prog->uniLocation("lm.decalSettings.blendMode"), mat->decalSettings->blendMode );
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.animatedDecalIgnoresTAA"), mat->decalSettings->animatedDecalIgnoresTAA );
	} else {
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.isDecal"), false );
	}
	if ( mat->flags & CE2Material::Flag_IsEffect ) {
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.useFallOff"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_UseFalloff) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.useRGBFallOff"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_UseRGBFalloff) );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.falloffStartAngle"), mat->effectSettings->falloffStartAngle );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.falloffStopAngle"), mat->effectSettings->falloffStopAngle );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.falloffStartOpacity"), mat->effectSettings->falloffStartOpacity );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.falloffStopOpacity"), mat->effectSettings->falloffStopOpacity );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.vertexColorBlend"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_VertexColorBlend) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.isAlphaTested"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_IsAlphaTested) );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.alphaTestThreshold"), mat->effectSettings->alphaThreshold );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.noHalfResOptimization"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_NoHalfResOpt) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.softEffect"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_SoftEffect) );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.softFalloffDepth"), mat->effectSettings->softFalloffDepth );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.emissiveOnlyEffect"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_EmissiveOnly) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.emissiveOnlyAutomaticallyApplied"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_EmissiveOnlyAuto) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.receiveDirectionalShadows"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_DirShadows) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.receiveNonDirectionalShadows"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_NonDirShadows) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.isGlass"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_IsGlass) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.frosting"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_Frosting) );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.frostingUnblurredBackgroundAlphaBlend"), mat->effectSettings->frostingBgndBlend );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.frostingBlurBias"), mat->effectSettings->frostingBlurBias );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.materialOverallAlpha"), mat->effectSettings->materialAlpha );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.zTest"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_ZTest) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.zWrite"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_ZWrite) );
		prog->uni1i_l( prog->uniLocation("lm.effectSettings.blendingMode"), mat->effectSettings->blendMode );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.backLightingEnable"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_BacklightEnable) );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.backlightingScale"), mat->effectSettings->backlightScale );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.backlightingSharpness"), mat->effectSettings->backlightSharpness );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.backlightingTransparencyFactor"), mat->effectSettings->backlightTransparency );
		prog->uni4f_l( prog->uniLocation("lm.effectSettings.backLightingTintColor"), mat->effectSettings->backlightTintColor );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.depthMVFixup"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_MVFixup) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.depthMVFixupEdgesOnly"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_MVFixupEdgesOnly) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.forceRenderBeforeOIT"), bool(mat->effectSettings->flags & CE2Material::EffectFlag_RenderBeforeOIT) );
		prog->uni1i_l( prog->uniLocation("lm.effectSettings.depthBiasInUlp"), mat->effectSettings->depthBias );
	}
	if ( mat->flags & CE2Material::Flag_HasOpacity ) {
		prog->uni1b_l( prog->uniLocation("lm.alphaSettings.hasOpacity"), true );
		prog->uni1f_l( prog->uniLocation("lm.alphaSettings.alphaTestThreshold"), mat->alphaThreshold );
		prog->uni1i_l( prog->uniLocation("lm.alphaSettings.opacitySourceLayer"), mat->alphaSourceLayer );
		prog->uni1i_l( prog->uniLocation("lm.alphaSettings.alphaBlenderMode"), mat->alphaBlendMode );
		prog->uni1b_l( prog->uniLocation("lm.alphaSettings.useDetailBlendMask"), bool(mat->flags & CE2Material::Flag_AlphaDetailBlendMask) );
		prog->uni1b_l( prog->uniLocation("lm.alphaSettings.useVertexColor"), bool(mat->flags & CE2Material::Flag_AlphaVertexColor) );
		prog->uni1i_l( prog->uniLocation("lm.alphaSettings.vertexColorChannel"), mat->alphaVertexColorChannel );
		const CE2Material::UVStream *	uvStream = mat->alphaUVStream;
		if ( !uvStream && (mat->layerMask & (1 << mat->alphaSourceLayer)) )
			uvStream = mat->layers[mat->alphaSourceLayer]->uvStream;
		FloatVector4	scaleAndOffset(1.0f, 1.0f, 0.0f, 0.0f);
		bool	useChannelTwo = false;
		if ( uvStream ) {
			scaleAndOffset = uvStream->scaleAndOffset;
			useChannelTwo = bool(uvStream->channel > 1);
		}
		prog->uni2f_l( prog->uniLocation("lm.alphaSettings.opacityUVstream.scale"), scaleAndOffset[0], scaleAndOffset[1] );
		prog->uni2f_l( prog->uniLocation("lm.alphaSettings.opacityUVstream.offset"), scaleAndOffset[2], scaleAndOffset[3] );
		prog->uni1b_l( prog->uniLocation("lm.alphaSettings.opacityUVstream.useChannelTwo"), useChannelTwo );
		prog->uni1f_l( prog->uniLocation("lm.alphaSettings.heightBlendThreshold"), mat->alphaHeightBlendThreshold );
		prog->uni1f_l( prog->uniLocation("lm.alphaSettings.heightBlendFactor"), mat->alphaHeightBlendFactor );
		prog->uni1f_l( prog->uniLocation("lm.alphaSettings.position"), mat->alphaPosition );
		prog->uni1f_l( prog->uniLocation("lm.alphaSettings.contrast"), mat->alphaContrast );
		prog->uni1b_l( prog->uniLocation("lm.alphaSettings.useDitheredTransparency"), bool(mat->flags & CE2Material::Flag_DitheredTransparency) );
	} else {
		prog->uni1b_l( prog->uniLocation("lm.alphaSettings.hasOpacity"), false );
	}

	for ( int i = 0; i < 4 && i < CE2Material::maxLayers; i++ ) {
		bool	layerEnabled = bool(mat->layerMask & (1 << i));
		prog->uni1b_l( prog->uniLocation("lm.layersEnabled[%d]", i), layerEnabled );
		if ( !layerEnabled )
			continue;
		const CE2Material::Layer *	layer = mat->layers[i];
		const CE2Material::Blender *	blender = nullptr;
		if ( i > 0 && i <= 3 && i <= CE2Material::maxBlenders )
			blender = mat->blenders[i - 1];
		if ( layer->material ) {
			prog->uni4f_l( prog->uniLocation("lm.layers[%d].material.color", i), layer->material->color, true );
			prog->uni1b_l( prog->uniLocation("lm.layers[%d].material.colorModeLerp", i), bool(layer->material->colorMode) );
		} else {
			prog->uni4f_l( prog->uniLocation("lm.layers[%d].material.color", i), FloatVector4(1.0f) );
			prog->uni1b_l( prog->uniLocation("lm.layers[%d].material.colorModeLerp", i), false );
		}
		if ( layer->material && layer->material->textureSet ) {
			const CE2Material::TextureSet *	textureSet = layer->material->textureSet;
			prog->uni1f_l( prog->uniLocation("lm.layers[%d].material.textureSet.floatParam", i), textureSet->floatParam );
			for ( int j = 0; j < 11 && j < CE2Material::TextureSet::maxTexturePaths; j++ ) {
				const std::string *	texturePath = nullptr;
				if ( textureSet->texturePathMask & (1 << j) )
					texturePath = textureSet->texturePaths[j];
				std::uint32_t	textureReplacement = textureSet->textureReplacements[j];
				int	textureReplacementMode = 0;
				if ( textureSet->textureReplacementMask & (1 << j) )
					textureReplacementMode = ( j == 0 || j == 7 ? 2 : ( j == 1 ? 3 : 1 ) );
				if ( j == 0 && ((scene->hasOption(Scene::DoLighting) && scene->hasVisMode(Scene::VisNormalsOnly)) || useErrorColor) ) {
					texturePath = nullptr;
					textureReplacement = (useErrorColor ? 0xFFFF00FFU : 0xFFFFFFFFU);
					textureReplacementMode = 1;
				}
				if ( j == 1 && !scene->hasOption(Scene::DoLighting) ) {
					texturePath = nullptr;
					textureReplacement = 0xFFFF8080U;
					textureReplacementMode = 3;
				}
				const CE2Material::UVStream *	uvStream = layer->uvStream;
				if ( j == 2 && i == mat->alphaSourceLayer && mat->alphaUVStream )
					uvStream = mat->alphaUVStream;
				prog->uniSampler_l( bsprop, texunit, prog->uniLocation("lm.layers[%d].material.textureSet.textures[%d]", i, j), prog->uniLocation("lm.layers[%d].material.textureSet.textureReplacements[%d]", i, j), texturePath, textureReplacement, textureReplacementMode, uvStream );
			}
		} else {
			prog->uni1f_l( prog->uniLocation("lm.layers[%d].material.textureSet.floatParam", i), 0.5f );
			for ( int j = 0; j < 11 && j < CE2Material::TextureSet::maxTexturePaths; j++ ) {
				prog->uniSampler_l( bsprop, texunit, prog->uniLocation("lm.layers[%d].material.textureSet.textures[%d]", i, j), prog->uniLocation("lm.layers[%d].material.textureSet.textureReplacements[%d]", i, j), nullptr, defaultSFTextureSet[j], int(j < 6), layer->uvStream );
			}
		}
		FloatVector4	scaleAndOffset(1.0f, 1.0f, 0.0f, 0.0f);
		bool	useChannelTwo = false;
		if ( layer->uvStream ) {
			scaleAndOffset = layer->uvStream->scaleAndOffset;
			useChannelTwo = (layer->uvStream->channel > 1);
		}
		prog->uni2f_l( prog->uniLocation("lm.layers[%d].uvStream.scale", i), scaleAndOffset[0], scaleAndOffset[1] );
		prog->uni2f_l( prog->uniLocation("lm.layers[%d].uvStream.offset", i), scaleAndOffset[2], scaleAndOffset[3] );
		prog->uni1b_l( prog->uniLocation("lm.layers[%d].uvStream.useChannelTwo", i), useChannelTwo );
		if ( blender ) {
			const CE2Material::UVStream *	uvStream = blender->uvStream;
			if ( uvStream ) {
				scaleAndOffset = uvStream->scaleAndOffset;
				useChannelTwo = (uvStream->channel > 1);
			}
			prog->uni2f_l( prog->uniLocation("lm.blenders[%d].uvStream.scale", i - 1), scaleAndOffset[0], scaleAndOffset[1] );
			prog->uni2f_l( prog->uniLocation("lm.blenders[%d].uvStream.offset", i - 1), scaleAndOffset[2], scaleAndOffset[3] );
			prog->uni1b_l( prog->uniLocation("lm.blenders[%d].uvStream.useChannelTwo", i - 1), useChannelTwo );
			prog->uniSampler_l( bsprop, texunit, prog->uniLocation("lm.blenders[%d].maskTexture", i - 1), prog->uniLocation("lm.blenders[%d].maskTextureReplacement", i - 1), blender->texturePath, blender->textureReplacement, int(blender->textureReplacementEnabled), uvStream );
			prog->uni1i_l( prog->uniLocation("lm.blenders[%d].blendMode", i - 1), int(blender->blendMode) );
			prog->uni1i_l( prog->uniLocation("lm.blenders[%d].colorChannel", i - 1), int(blender->colorChannel) );
			for ( int j = 0; j < CE2Material::Blender::maxFloatParams; j++ )
				prog->uni1f_l( prog->uniLocation("lm.blenders[%d].floatParams[%d]", i - 1, j), blender->floatParams[j]);
			for ( int j = 0; j < CE2Material::Blender::maxBoolParams; j++ )
				prog->uni1b_l( prog->uniLocation("lm.blenders[%d].boolParams[%d]", i - 1, j), blender->boolParams[j]);
		}
	}

	prog->uni4m( MAT_VIEW, mesh->viewTrans().toMatrix4() );
	prog->uni4m( MAT_WORLD, mesh->worldTrans().toMatrix4() );

	QMapIterator<int, Program::CoordType> itx( prog->texcoords );

	while ( itx.hasNext() ) {
		itx.next();

		if ( !activateTextureUnit( itx.key() ) )
			return false;

		auto it = itx.value();
		if ( it == Program::CT_TANGENT ) {
			if ( mesh->transTangents.count() ) {
				glEnableClientState( GL_TEXTURE_COORD_ARRAY );
				glTexCoordPointer( 3, GL_FLOAT, 0, mesh->transTangents.constData() );
			} else if ( mesh->tangents.count() ) {
				glEnableClientState( GL_TEXTURE_COORD_ARRAY );
				glTexCoordPointer( 3, GL_FLOAT, 0, mesh->tangents.constData() );
			} else {
				return false;
			}

		} else if ( it == Program::CT_BITANGENT ) {
			if ( mesh->transBitangents.count() ) {
				glEnableClientState( GL_TEXTURE_COORD_ARRAY );
				glTexCoordPointer( 3, GL_FLOAT, 0, mesh->transBitangents.constData() );
			} else if ( mesh->bitangents.count() ) {
				glEnableClientState( GL_TEXTURE_COORD_ARRAY );
				glTexCoordPointer( 3, GL_FLOAT, 0, mesh->bitangents.constData() );
			} else {
				return false;
			}
		} else if ( bsprop ) {
			int txid = it;
			if ( txid < 0 )
				return false;

			const MeshFile *	sfMesh = static_cast< BSMesh * >(mesh)->meshSelected;
			if ( !sfMesh || sfMesh->coords.count() != sfMesh->positions.count() )
				return false;

			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			glTexCoordPointer( 4, GL_FLOAT, 0, sfMesh->coords.constData() );
		}
	}

	// setup lighting

	//glEnable( GL_LIGHTING );

	// setup blending

	if ( mat && scene->hasOption(Scene::DoBlending) ) {
		static const GLenum blendMapS[8] = {
			GL_SRC_ALPHA,	// "AlphaBlend"
			GL_SRC_ALPHA,	// "Additive"
			GL_SRC_ALPHA,	// "SourceSoftAdditive" (TODO: not implemented yet)
			GL_DST_COLOR,	// "Multiply"
			GL_SRC_ALPHA,	// "DestinationSoftAdditive" (not implemented)
			GL_SRC_ALPHA,	// "DestinationInvertedSoftAdditive" (not implemented)
			GL_SRC_ALPHA,	// "TakeSmaller" (not implemented)
			GL_ZERO	// "None"
		};
		static const GLenum blendMapD[8] = {
			GL_ONE_MINUS_SRC_ALPHA,	// "AlphaBlend"
			GL_ONE,	// "Additive"
			GL_ONE,	// "SourceSoftAdditive"
			GL_ZERO,	// "Multiply"
			GL_ONE_MINUS_SRC_ALPHA,	// "DestinationSoftAdditive"
			GL_ONE_MINUS_SRC_ALPHA,	// "DestinationInvertedSoftAdditive"
			GL_ONE_MINUS_SRC_ALPHA,	// "TakeSmaller"
			GL_ONE	// "None"
		};

		if ( mat->flags & CE2Material::Flag_AlphaBlending ) {
			glEnable( GL_BLEND );
			if ( mat->flags & CE2Material::Flag_IsEffect ) {
				if ( mat->effectSettings->flags & (CE2Material::EffectFlag_EmissiveOnly | CE2Material::EffectFlag_EmissiveOnlyAuto) )
					glBlendFunc( GL_ONE, GL_ONE );
				else
					glBlendFunc( blendMapS[mat->effectSettings->blendMode], blendMapD[mat->effectSettings->blendMode] );
			} else if ( mat->flags & CE2Material::Flag_IsDecal )
				glBlendFunc( blendMapS[mat->decalSettings->blendMode], blendMapD[mat->decalSettings->blendMode] );
		} else {
			glDisable( GL_BLEND );
		}

		if ( (mat->flags & CE2Material::Flag_IsEffect) && (mat->effectSettings->flags & CE2Material::EffectFlag_IsAlphaTested) ) {
			glEnable( GL_ALPHA_TEST );
			glAlphaFunc( GL_GREATER, mat->effectSettings->alphaThreshold );
		} else if ( mat->flags & CE2Material::Flag_HasOpacity && mat->alphaThreshold > 0.0f ) {
			glEnable( GL_ALPHA_TEST );
			glAlphaFunc( GL_GREATER, mat->alphaThreshold );
		} else {
			glDisable( GL_ALPHA_TEST );
		}

		if ( mat->flags & CE2Material::Flag_IsDecal ) {
			glEnable( GL_POLYGON_OFFSET_FILL );
			glPolygonOffset( -1.0f, -1.0f );
		}
	}

	glDisable( GL_COLOR_MATERIAL );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	if ( mat->flags & CE2Material::Flag_TwoSided ) {
		glDisable( GL_CULL_FACE );
	} else {
		glEnable( GL_CULL_FACE );
		glCullFace( GL_BACK );
	}
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

	if ( !mesh->depthTest ) {
		glDisable( GL_DEPTH_TEST );
	}

	if ( !mesh->depthWrite || mesh->translucent ) {
		glDepthMask( GL_FALSE );
	}

	return true;
}

bool Renderer::setupProgram( Program * prog, Shape * mesh, const PropertyList & props,
								const QVector<QModelIndex> & iBlocks, bool eval )
{
	auto nif = NifModel::fromValidIndex( mesh->index() );
	if ( !nif )
		return false;
	if ( eval && !prog->conditions.eval( nif, iBlocks ) )
		return false;
	if ( nif->getBSVersion() >= 160 )
		return setupProgramSF( prog, mesh );

	fn->glUseProgram( prog->id );

	auto nifVersion = nif->getBSVersion();
	auto scene = mesh->scene;
	auto lsp = mesh->bslsp;
	auto esp = mesh->bsesp;

	Material * mat = nullptr;
	if ( lsp )
		mat = lsp->getMaterial();
	else if ( esp )
		mat = esp->getMaterial();

	QString default_n = (nifVersion >= 151) ? ::default_ns : ::default_n;

	// texturing

	TexturingProperty * texprop = props.get<TexturingProperty>();
	BSShaderLightingProperty * bsprop = mesh->bssp;
	// BSShaderLightingProperty * bsprop = props.get<BSShaderLightingProperty>();
	// TODO: BSLSP has been split off from BSShaderLightingProperty so it needs
	//	to be accessible from here

	TexClampMode clamp = TexClampMode::WRAP_S_WRAP_T;
	if ( lsp )
		clamp = lsp->clampMode;

	int texunit = 0;
	if ( bsprop ) {
		QString forced;
		if ( scene->hasOption(Scene::DoLighting) && scene->hasVisMode(Scene::VisNormalsOnly) )
			forced = white;

		QString alt = white;
		if ( scene->hasOption(Scene::DoErrorColor) )
			alt = magenta;

		prog->uniSampler( bsprop, SAMP_BASE, 0, texunit, alt, clamp, forced );
	} else {
		GLint uniBaseMap = prog->uniformLocations[SAMP_BASE];
		if ( uniBaseMap >= 0 && (texprop || (bsprop && lsp)) ) {
			if ( !activateTextureUnit( texunit ) || (texprop && !texprop->bind( 0 )) )
				prog->uniSamplerBlank( SAMP_BASE, texunit );
			else
				fn->glUniform1i( uniBaseMap, texunit++ );
		}
	}

	if ( bsprop && !esp ) {
		QString forced;
		if ( !scene->hasOption(Scene::DoLighting) )
			forced = default_n;
		prog->uniSampler( bsprop, SAMP_NORMAL, 1, texunit, default_n, clamp, forced );
	} else if ( !bsprop ) {
		GLint uniNormalMap = prog->uniformLocations[SAMP_NORMAL];
		if ( uniNormalMap >= 0 && texprop ) {
			bool result = true;
			if ( texprop ) {
				QString fname = texprop->fileName( 0 );
				if ( !fname.isEmpty() ) {
					int pos = fname.lastIndexOf( "_" );
					if ( pos >= 0 )
						fname = fname.left( pos ) + "_n.dds";
					else if ( (pos = fname.lastIndexOf( "." )) >= 0 )
						fname = fname.insert( pos, "_n" );
				}

				if ( !fname.isEmpty() && (!activateTextureUnit( texunit ) || !texprop->bind( 0, fname )) )
					result = false;
			}

			if ( !result )
				prog->uniSamplerBlank( SAMP_NORMAL, texunit );
			else
				fn->glUniform1i( uniNormalMap, texunit++ );
		}
	}

	if ( bsprop && !esp ) {
		prog->uniSampler( bsprop, SAMP_GLOW, 2, texunit, black, clamp );
	} else if ( !bsprop ) {
		GLint uniGlowMap = prog->uniformLocations[SAMP_GLOW];
		if ( uniGlowMap >= 0 && texprop ) {
			bool result = true;
			if ( texprop ) {
				QString fname = texprop->fileName( 0 );
				if ( !fname.isEmpty() ) {
					int pos = fname.lastIndexOf( "_" );
					if ( pos >= 0 )
						fname = fname.left( pos ) + "_g.dds";
					else if ( (pos = fname.lastIndexOf( "." )) >= 0 )
						fname = fname.insert( pos, "_g" );
				}

				if ( !fname.isEmpty() && (!activateTextureUnit( texunit ) || !texprop->bind( 0, fname )) )
					result = false;
			}

			if ( !result )
				prog->uniSamplerBlank( SAMP_GLOW, texunit );
			else
				fn->glUniform1i( uniGlowMap, texunit++ );
		}
	}

	// BSLightingShaderProperty
	if ( lsp ) {
		prog->uni1f( LIGHT_EFF1, lsp->lightingEffect1 );
		prog->uni1f( LIGHT_EFF2, lsp->lightingEffect2 );

		prog->uni1f( ALPHA, lsp->alpha );

		prog->uni2f( UV_SCALE, lsp->uvScale.x, lsp->uvScale.y );
		prog->uni2f( UV_OFFSET, lsp->uvOffset.x, lsp->uvOffset.y );

		prog->uni4m( MAT_VIEW, mesh->viewTrans().toMatrix4() );
		prog->uni4m( MAT_WORLD, mesh->worldTrans().toMatrix4() );

		prog->uni1i( G2P_COLOR, lsp->greyscaleColor );
		prog->uniSampler( bsprop, SAMP_GRAYSCALE, 3, texunit, "", TexClampMode::MIRRORED_S_MIRRORED_T );

		prog->uni1i( HAS_TINT_COLOR, lsp->hasTintColor );
		if ( lsp->hasTintColor ) {
			prog->uni3f( TINT_COLOR, lsp->tintColor.red(), lsp->tintColor.green(), lsp->tintColor.blue() );
		}

		prog->uni1i( HAS_MAP_DETAIL, lsp->hasDetailMask );
		prog->uniSampler( bsprop, SAMP_DETAIL, 3, texunit, "shaders/blankdetailmap.dds", clamp );

		prog->uni1i( HAS_MAP_TINT, lsp->hasTintMask );
		prog->uniSampler( bsprop, SAMP_TINT, 6, texunit, gray, clamp );

		// Rim & Soft params

		prog->uni1i( HAS_SOFT, lsp->hasSoftlight );
		prog->uni1i( HAS_RIM, lsp->hasRimlight );

		prog->uniSampler( bsprop, SAMP_LIGHT, 2, texunit, default_n, clamp );

		// Backlight params

		prog->uni1i( HAS_MAP_BACK, lsp->hasBacklight );

		prog->uniSampler( bsprop, SAMP_BACKLIGHT, 7, texunit, default_n, clamp );

		// Glow params

		if ( scene->hasOption(Scene::DoGlow) && scene->hasOption(Scene::DoLighting) && (lsp->hasEmittance || nifVersion >= 151) )
			prog->uni1f( GLOW_MULT, lsp->emissiveMult );
		else
			prog->uni1f( GLOW_MULT, 0 );

		prog->uni1i( HAS_EMIT, lsp->hasEmittance );
		prog->uni1i( HAS_MAP_GLOW, lsp->hasGlowMap );
		prog->uni3f( GLOW_COLOR, lsp->emissiveColor.red(), lsp->emissiveColor.green(), lsp->emissiveColor.blue() );

		// Specular params
		float s = ( scene->hasOption(Scene::DoSpecular) && scene->hasOption(Scene::DoLighting) ) ? lsp->specularStrength : 0.0;
		prog->uni1f( SPEC_SCALE, s );

		// Assure specular power does not break the shaders
		prog->uni1f( SPEC_GLOSS, lsp->specularGloss);
		prog->uni3f( SPEC_COLOR, lsp->specularColor.red(), lsp->specularColor.green(), lsp->specularColor.blue() );
		prog->uni1i( HAS_MAP_SPEC, lsp->hasSpecularMap );

		if ( nifVersion <= 130 ) {
			if ( nifVersion == 130 || (lsp->hasSpecularMap && !lsp->hasBacklight) )
				prog->uniSampler( bsprop, SAMP_SPECULAR, 7, texunit, white, clamp );
			else
				prog->uniSampler( bsprop, SAMP_SPECULAR, 7, texunit, black, clamp );
		}

		if ( nifVersion >= 130 ) {
			prog->uni1i( DOUBLE_SIDE, lsp->isDoubleSided );
			prog->uni1f( G2P_SCALE, lsp->paletteScale );
			prog->uni1f( SS_ROLLOFF, lsp->lightingEffect1 );
			prog->uni1f( POW_FRESNEL, lsp->fresnelPower );
			prog->uni1f( POW_RIM, lsp->rimPower );
			prog->uni1f( POW_BACK, lsp->backlightPower );
		}

		// Multi-Layer

		prog->uniSampler( bsprop, SAMP_INNER, 6, texunit, default_n, clamp );
		if ( lsp->hasMultiLayerParallax ) {
			prog->uni2f( INNER_SCALE, lsp->innerTextureScale.x, lsp->innerTextureScale.y );
			prog->uni1f( INNER_THICK, lsp->innerThickness );

			prog->uni1f( OUTER_REFR, lsp->outerRefractionStrength );
			prog->uni1f( OUTER_REFL, lsp->outerReflectionStrength );
		}

		// Environment Mapping

		prog->uni1i( HAS_MAP_CUBE, scene->hasOption(Scene::DoCubeMapping) && scene->hasOption(Scene::DoLighting) && (lsp->hasEnvironmentMap || nifVersion >= 151) );
		prog->uni1i( HAS_MASK_ENV, lsp->useEnvironmentMask );
		float refl = ( nifVersion < 151 ? lsp->environmentReflection : 1.0f );
		prog->uni1f( ENV_REFLECTION, refl );

		// Always bind cube regardless of shader settings
		GLint uniCubeMap = prog->uniformLocations[SAMP_CUBE];
		if ( uniCubeMap >= 0 ) {
			QString	fname = bsprop->fileName( 4 );
			QString	cube = (nifVersion < 128 ? cube_sk : cube_fo4_76);
			if ( fname.isEmpty() )
				fname = cube;

			if ( !activateTextureUnit( texunit ) || !bsprop->bindCube( fname ) )
				if ( !activateTextureUnit( texunit ) || !bsprop->bindCube( cube ) )
					return false;

			fn->glUniform1i( uniCubeMap, texunit++ );
		}
		// Always bind mask regardless of shader settings
		prog->uniSampler( bsprop, SAMP_ENV_MASK, 5, texunit, white, clamp );

		if ( nifVersion >= 151 ) {
			prog->uniSampler( bsprop, SAMP_REFLECTIVITY, 8, texunit, reflectivity, clamp );
			prog->uniSampler( bsprop, SAMP_LIGHTING, 9, texunit, lighting, clamp );
		}

		// Parallax
		prog->uni1i( HAS_MAP_HEIGHT, lsp->hasHeightMap );
		prog->uniSampler( bsprop, SAMP_HEIGHT, 3, texunit, gray, clamp );
	}


	// BSEffectShaderProperty
	if ( esp ) {

		prog->uni4m( MAT_WORLD, mesh->worldTrans().toMatrix4() );

		clamp = esp->clampMode;

		prog->uniSampler( bsprop, SAMP_BASE, 0, texunit, white, clamp );

		prog->uni1i( DOUBLE_SIDE, esp->isDoubleSided );

		prog->uni2f( UV_SCALE, esp->uvScale.x, esp->uvScale.y );
		prog->uni2f( UV_OFFSET, esp->uvOffset.x, esp->uvOffset.y );

		prog->uni1i( HAS_MAP_BASE, esp->hasSourceTexture );
		prog->uni1i( HAS_MAP_G2P, esp->hasGreyscaleMap );

		prog->uni1i( G2P_ALPHA, esp->greyscaleAlpha );
		prog->uni1i( G2P_COLOR, esp->greyscaleColor );


		prog->uni1i( USE_FALLOFF, esp->useFalloff );
		prog->uni1i( HAS_RGBFALL, esp->hasRGBFalloff );
		prog->uni1i( HAS_WEAP_BLOOD, esp->hasWeaponBlood );

		// Glow params

		prog->uni4f( GLOW_COLOR, esp->emissiveColor.red(), esp->emissiveColor.green(), esp->emissiveColor.blue(), esp->emissiveColor.alpha() );
		prog->uni1f( GLOW_MULT, esp->emissiveMult );

		// Falloff params

		prog->uni4f( FALL_PARAMS,
			esp->falloff.startAngle, esp->falloff.stopAngle,
			esp->falloff.startOpacity, esp->falloff.stopOpacity
		);

		prog->uni1f( FALL_DEPTH, esp->falloff.softDepth );

		// BSEffectShader textures
		prog->uniSampler( bsprop, SAMP_GRAYSCALE, 1, texunit, "", TexClampMode::MIRRORED_S_MIRRORED_T );

		if ( nifVersion >= 130 ) {

			prog->uni1f( LIGHT_INF, esp->lightingInfluence );

			prog->uni1i( HAS_MAP_NORMAL, esp->hasNormalMap && scene->hasOption(Scene::DoLighting) );

			prog->uniSampler( bsprop, SAMP_NORMAL, 3, texunit, default_n, clamp );

			prog->uni1i( HAS_MAP_CUBE, esp->hasEnvironmentMap );
			prog->uni1i( HAS_MASK_ENV, esp->hasEnvironmentMask );
			float refl = 0.0;
			if ( esp->hasEnvironmentMap && scene->hasOption(Scene::DoCubeMapping) && scene->hasOption(Scene::DoLighting) )
				refl = esp->environmentReflection;

			prog->uni1f( ENV_REFLECTION, refl );

			GLint uniCubeMap = prog->uniformLocations[SAMP_CUBE];
			if ( uniCubeMap >= 0 ) {
				QString fname = bsprop->fileName( 2 );
				QString	cube = (nifVersion < 128 ? cube_sk : cube_fo4_76);
				if ( fname.isEmpty() )
					fname = cube;

				if ( !activateTextureUnit( texunit ) || !bsprop->bindCube( fname ) )
					if ( !activateTextureUnit( texunit ) || !bsprop->bindCube( cube ) )
						return false;


				fn->glUniform1i( uniCubeMap, texunit++ );
			}
			prog->uniSampler( bsprop, SAMP_SPECULAR, 4, texunit, white, clamp );
			if ( nifVersion >= 151 ) {
				prog->uniSampler( bsprop, SAMP_REFLECTIVITY, 6, texunit, reflectivity, clamp );
				prog->uniSampler( bsprop, SAMP_LIGHTING, 7, texunit, lighting, clamp );
			}

			prog->uni1f( LUM_EMIT, esp->lumEmittance );
		}
	}

	// Defaults for uniforms in older meshes
	if ( !esp && !lsp ) {
		prog->uni2f( UV_SCALE, 1.0, 1.0 );
		prog->uni2f( UV_OFFSET, 0.0, 0.0 );
	}

	QMapIterator<int, Program::CoordType> itx( prog->texcoords );

	while ( itx.hasNext() ) {
		itx.next();

		if ( !activateTextureUnit( itx.key() ) )
			return false;

		auto it = itx.value();
		if ( it == Program::CT_TANGENT ) {
			if ( mesh->transTangents.count() ) {
				glEnableClientState( GL_TEXTURE_COORD_ARRAY );
				glTexCoordPointer( 3, GL_FLOAT, 0, mesh->transTangents.constData() );
			} else if ( mesh->tangents.count() ) {
				glEnableClientState( GL_TEXTURE_COORD_ARRAY );
				glTexCoordPointer( 3, GL_FLOAT, 0, mesh->tangents.constData() );
			} else {
				return false;
			}

		} else if ( it == Program::CT_BITANGENT ) {
			if ( mesh->transBitangents.count() ) {
				glEnableClientState( GL_TEXTURE_COORD_ARRAY );
				glTexCoordPointer( 3, GL_FLOAT, 0, mesh->transBitangents.constData() );
			} else if ( mesh->bitangents.count() ) {
				glEnableClientState( GL_TEXTURE_COORD_ARRAY );
				glTexCoordPointer( 3, GL_FLOAT, 0, mesh->bitangents.constData() );
			} else {
				return false;
			}
		} else if ( texprop ) {
			int txid = it;
			if ( txid < 0 )
				return false;

			int set = texprop->coordSet( txid );

			if ( set < 0 || !(set < mesh->coords.count()) || !mesh->coords[set].count() )
				return false;

			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			glTexCoordPointer( 2, GL_FLOAT, 0, mesh->coords[set].constData() );
		} else if ( bsprop ) {
			int txid = it;
			if ( txid < 0 )
				return false;

			int set = 0;

			if ( set < 0 || !(set < mesh->coords.count()) || !mesh->coords[set].count() )
				return false;

			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			glTexCoordPointer( 2, GL_FLOAT, 0, mesh->coords[set].constData() );
		}
	}

	// setup lighting

	//glEnable( GL_LIGHTING );

	// setup blending

	glProperty( mesh->alphaProperty );

	if ( mat && scene->hasOption(Scene::DoBlending) ) {
		static const GLenum blendMap[11] = {
			GL_ONE, GL_ZERO, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
			GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA_SATURATE
		};

		if ( mat->hasAlphaBlend() ) {
			glEnable( GL_BLEND );
			glBlendFunc( blendMap[mat->iAlphaSrc], blendMap[mat->iAlphaDst] );
		} else {
			glDisable( GL_BLEND );
		}

		if ( mat->hasAlphaTest() ) {
			glEnable( GL_ALPHA_TEST );
			glAlphaFunc( GL_GREATER, float( mat->iAlphaTestRef ) / 255.0 );
		} else {
			glDisable( GL_ALPHA_TEST );
		}

		if ( mat && mat->bDecal ) {
			glEnable( GL_POLYGON_OFFSET_FILL );
			glPolygonOffset( -1.0f, -1.0f );
		}
	}

	// BSESP/BSLSP do not always need an NiAlphaProperty, and appear to override it at times
	if ( !mat && mesh->translucent ) {
		glEnable( GL_BLEND );
		glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		// If mesh is alpha tested, override threshold
		glAlphaFunc( GL_GREATER, 0.1f );
	}

	glDisable( GL_COLOR_MATERIAL );

	if ( nifVersion < 83 ) {
		// setup vertex colors

		//glProperty( props.get< VertexColorProperty >(), glIsEnabled( GL_COLOR_ARRAY ) );

		// setup material

		glProperty( props.get<MaterialProperty>(), props.get<SpecularProperty>() );

		// setup z buffer

		glProperty( props.get<ZBufferProperty>() );

		// setup stencil

		glProperty( props.get<StencilProperty>() );

		// wireframe ?

		glProperty( props.get<WireframeProperty>() );
	} else {
		glEnable( GL_DEPTH_TEST );
		glDepthMask( GL_TRUE );
		glDepthFunc( GL_LEQUAL );
		glEnable( GL_CULL_FACE );
		glCullFace( GL_BACK );
		glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	}

	if ( !mesh->depthTest ) {
		glDisable( GL_DEPTH_TEST );
	}

	if ( !mesh->depthWrite || mesh->translucent ) {
		glDepthMask( GL_FALSE );
	}

	return true;
}

void Renderer::setupFixedFunction( Shape * mesh, const PropertyList & props )
{
	// setup lighting

	glEnable( GL_LIGHTING );

	// Disable specular because it washes out vertex colors
	//	at perpendicular viewing angles
	float color[4] = { 0, 0, 0, 0 };
	glMaterialfv( GL_FRONT_AND_BACK, GL_SPECULAR, color );
	glLightfv( GL_LIGHT0, GL_SPECULAR, color );

	// setup blending

	glProperty( mesh->alphaProperty );

	// setup vertex colors

	glProperty( props.get<VertexColorProperty>(), glIsEnabled( GL_COLOR_ARRAY ) );

	// setup material

	glProperty( props.get<MaterialProperty>(), props.get<SpecularProperty>() );

	// setup texturing

	//glProperty( props.get< TexturingProperty >() );

	// setup z buffer

	glProperty( props.get<ZBufferProperty>() );

	if ( !mesh->depthTest ) {
		glDisable( GL_DEPTH_TEST );
	}

	if ( !mesh->depthWrite ) {
		glDepthMask( GL_FALSE );
	}

	// setup stencil

	glProperty( props.get<StencilProperty>() );

	// wireframe ?

	glProperty( props.get<WireframeProperty>() );

	// normalize

	if ( glIsEnabled( GL_NORMAL_ARRAY ) )
		glEnable( GL_NORMALIZE );
	else
		glDisable( GL_NORMALIZE );

	// setup texturing

	if ( !mesh->scene->hasOption(Scene::DoTexturing) )
		return;

	if ( TexturingProperty * texprop = props.get<TexturingProperty>() ) {
		// standard multi texturing property
		int stage = 0;

		if ( texprop->bind( 1, mesh->coords, stage ) ) {
			// dark
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA );

			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0 );
		}

		if ( texprop->bind( 0, mesh->coords, stage ) ) {
			// base
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA );

			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0 );
		}

		if ( texprop->bind( 2, mesh->coords, stage ) ) {
			// detail
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA );

			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0 );
		}

		if ( texprop->bind( 6, mesh->coords, stage ) ) {
			// decal 0
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );

			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0 );
		}

		if ( texprop->bind( 7, mesh->coords, stage ) ) {
			// decal 1
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );

			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0 );
		}

		if ( texprop->bind( 8, mesh->coords, stage ) ) {
			// decal 2
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );

			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0 );
		}

		if ( texprop->bind( 9, mesh->coords, stage ) ) {
			// decal 3
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );

			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0 );
		}

		if ( texprop->bind( 4, mesh->coords, stage ) ) {
			// glow
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_ADD );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );

			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0 );
		}
	} else if ( TextureProperty * texprop = props.get<TextureProperty>() ) {
		// old single texture property
		texprop->bind( mesh->coords );
	} else if ( BSShaderLightingProperty * texprop = props.get<BSShaderLightingProperty>() ) {
		// standard multi texturing property
		int stage = 0;

		if ( texprop->bind( 0, mesh->coords ) ) {
			//, mesh->coords, stage ) )
			// base
			stage++;
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR );

			glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );
			glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_TEXTURE );
			glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA );

			glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0 );
		}
	} else {
		glDisable( GL_TEXTURE_2D );
	}
}

