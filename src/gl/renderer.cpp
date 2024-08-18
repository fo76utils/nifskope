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
#include "gl/BSMesh.h"
#include "libfo76utils/src/ddstxt16.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSettings>
#include <QTextStream>
#include <chrono>


//! @file renderer.cpp Renderer and child classes implementation

static bool shader_initialized = false;
static bool shader_ready = true;

static QString white = "#FFFFFFFF";
static QString black = "#FF000000";
static QString lighting = "#FF00F040";
static QString reflectivity = "#FF0A0A0A";
static QString gray = "#FF808080s";
static QString magenta = "#FFFF00FF";
static QString default_n = "#FFFF8080";
static QString default_ns = "#FFFF8080n";
static QString cube_sk = "textures/cubemaps/bleakfallscube_e.dds";
static QString cube_fo4 = "textures/shared/cubemaps/mipblur_defaultoutside1.dds";
static QString pbr_lut_sf = "#sfpbr.dds";

static const std::uint32_t defaultSFTextureSet[21] = {
	0xFFFF00FFU, 0xFFFF8080U, 0xFFFFFFFFU, 0xFFC0C0C0U, 0xFF000000U, 0xFFFFFFFFU,
	0xFF000000U, 0xFF000000U, 0xFF000000U, 0xFF808080U, 0xFF000000U, 0xFF808080U,
	0xFF000000U, 0xFF000000U, 0xFF808080U, 0xFF808080U, 0xFF808080U, 0xFF000000U,
	0xFF000000U, 0xFFFFFFFFU, 0xFF808080U
};

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
		int	n = data.indexOf( "SF_NUM_TEXTURE_UNITS" );
		if ( n >= 0 )
			data.replace( n, 20, QByteArray::number( TexCache::num_texture_units - 2 ) );

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
	: f( fn ), name( n.toLower() ), id( 0 )
{
	uniLocationsMap = new UniformLocationMapItem[512];
	uniLocationsMapMask = 511;
	uniLocationsMapSize = 0;
	id = f->glCreateProgram();
}

Renderer::Program::~Program()
{
	if ( id )
		f->glDeleteShader( id );
	delete[] uniLocationsMap;
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
					throw QString( "texture unit %1 is assigned twice" ).arg( unit );

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
	int	tmp = settings.value( "Settings/Render/General/Cube Map Bgnd", 1 ).toInt();
	cfg.cubeBgndMipLevel = std::int8_t( std::min< int >( std::max< int >( tmp, -1 ), 6 ) );
	cfg.sfParallaxMaxSteps = short( settings.value( "Settings/Render/General/Sf Parallax Steps", 200 ).toInt() );
	cfg.sfParallaxScale = settings.value( "Settings/Render/General/Sf Parallax Scale", 0.0f).toFloat();
	cfg.sfParallaxOffset = settings.value( "Settings/Render/General/Sf Parallax Offset", 0.5f).toFloat();
	cfg.cubeMapPathFO76 = settings.value( "Settings/Render/General/Cube Map Path FO 76", "textures/shared/cubemaps/mipblur_defaultoutside1.dds" ).toString();
	cfg.cubeMapPathSTF = settings.value( "Settings/Render/General/Cube Map Path STF", "textures/cubemaps/cell_cityplazacube.dds" ).toString();
	TexCache::loadSettings( settings );

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
	const NifModel *	nif;
	if ( !shader_ready
		|| hint.isNull()
		|| ( nif = mesh->scene->nifModel ) == nullptr
		|| ( nif->getBSVersion() == 0 )
		|| mesh->scene->hasOption(Scene::DisableShaders) ) {
		setupFixedFunction( mesh );
		return QString();
	}

	if ( !hint.isEmpty() ) {
		Program * program = programs.value( hint );
		if ( program && program->status ) {
			fn->glUseProgram( program->id );
			bool	setupStatus;
			if ( nif->getBSVersion() >= 170 )
				setupStatus = setupProgramCE2( nif, program, mesh );
			else if ( nif->getBSVersion() >= 83 )
				setupStatus = setupProgramCE1( nif, program, mesh );
			else
				setupStatus = setupProgramFO3( nif, program, mesh );
			if ( setupStatus )
				return program->name;
			stopProgram();
		}
	}

	QVector<QModelIndex> iBlocks;
	iBlocks << mesh->index();
	iBlocks << mesh->iData;
	{
		PropertyList props;
		mesh->activeProperties( props );

		for ( Property * p : props ) {
			iBlocks.append( p->index() );
		}
	}

	for ( Program * program : programs ) {
		if ( program->status && program->conditions.eval( nif, iBlocks ) ) {
			fn->glUseProgram( program->id );
			bool	setupStatus;
			if ( nif->getBSVersion() >= 170 )
				setupStatus = setupProgramCE2( nif, program, mesh );
			else if ( nif->getBSVersion() >= 83 )
				setupStatus = setupProgramCE1( nif, program, mesh );
			else
				setupStatus = setupProgramFO3( nif, program, mesh );
			if ( setupStatus )
				return program->name;
			stopProgram();
		}
	}

	setupFixedFunction( mesh );
	return QString();
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
	if ( uniSamp < 0 )
		return true;
	if ( !activateTextureUnit( texunit ) )
		return false;

	// TODO: On stream 155 bsprop->fileName can reference incorrect strings because
	// the BSSTS is not filled out nor linked from the BSSP
	do {
		if ( !forced.isEmpty() && bsprop->bind( forced, true, TexClampMode(clamp) ) )
			break;
		if ( textureSlot >= 0 ) {
			QString	fname = bsprop->fileName( textureSlot );
			if ( !fname.isEmpty() && bsprop->bind( fname, false, TexClampMode(clamp) ) )
				break;
		}
		if ( !alternate.isEmpty() && bsprop->bind( alternate, false, TexClampMode::WRAP_S_WRAP_T ) )
			break;
		const QString *	fname = &black;
		if ( textureSlot == 0 )
			fname = &white;
		else if ( textureSlot == 1 )
			fname = ( bsprop->bsVersion < 151 ? &default_n : &default_ns );
		else if ( textureSlot >= 8 && bsprop->bsVersion >= 151 )
			fname = ( textureSlot == 8 ? &reflectivity : &lighting );
		if ( bsprop->bind( *fname, true, TexClampMode::WRAP_S_WRAP_T ) )
			break;

		return false;
	} while ( false );

	f->glUniform1i( uniSamp, texunit++ );
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

inline Renderer::Program::UniformLocationMapItem::UniformLocationMapItem( const char *s, int argsX16Y16 )
	: fmt( s ), args( std::uint32_t(argsX16Y16) ), l( -1 )
{
}

inline bool Renderer::Program::UniformLocationMapItem::operator==( const UniformLocationMapItem & r ) const
{
	return ( fmt == r.fmt && args == r.args );
}

inline std::uint32_t Renderer::Program::UniformLocationMapItem::hashFunction() const
{
	std::uint32_t	h = 0xFFFFFFFFU;
	// note: this requires fmt to point to a string literal
	hashFunctionCRC32C< std::uint64_t >( h, reinterpret_cast< std::uintptr_t >( fmt ) );
	hashFunctionCRC32C< std::uint32_t >( h, args );
	return h;
}

int Renderer::Program::storeUniformLocation( const UniformLocationMapItem & o, size_t i )
{
	const char *	fmt = o.fmt;
	int	arg1 = int( o.args & 0xFFFF );
	int	arg2 = int( o.args >> 16 );

	char	varNameBuf[256];
	char *	sp = varNameBuf;
	char *	endp = sp + 254;
	while ( sp < endp ) [[likely]] {
		char	c = *( fmt++ );
		if ( (unsigned char) c > (unsigned char) '%' ) [[likely]] {
			*( sp++ ) = c;
			continue;
		}
		if ( !c )
			break;
		if ( c == '%' ) [[likely]] {
			c = *( fmt++ );
			if ( c == 'd' ) {
				int	n = arg1;
				arg1 = arg2;
				if ( n >= 10 ) {
					c = char( (n / 10) & 15 ) | '0';
					*( sp++ ) = c;
					n = n % 10;
				}
				c = char( n & 15 ) | '0';
			} else if ( c != '%' ) {
				break;
			}
		}
		*( sp++ ) = c;
	}
	*sp = '\0';
	int	l = f->glGetUniformLocation( id, varNameBuf );
	uniLocationsMap[i] = o;
	uniLocationsMap[i].l = l;
	if ( l < 0 )
		std::fprintf( stderr, "[Warning] Uniform '%s' not found\n", varNameBuf );

	uniLocationsMapSize++;
	if ( ( uniLocationsMapSize * size_t(3) ) > ( uniLocationsMapMask * size_t(2) ) ) {
		unsigned int	m = ( uniLocationsMapMask << 1 ) | 0xFFU;
		UniformLocationMapItem *	tmpBuf = new UniformLocationMapItem[m + 1U];
		for ( size_t j = 0; j <= uniLocationsMapMask; j++ ) {
			size_t	k = uniLocationsMap[j].hashFunction() & m;
			while ( tmpBuf[k].fmt )
				k = ( k + 1 ) & m;
			tmpBuf[k] = uniLocationsMap[j];
		}
		delete[] uniLocationsMap;
		uniLocationsMap = tmpBuf;
		uniLocationsMapMask = m;
	}

	return l;
}

int Renderer::Program::uniLocation( const char * fmt, int argsX16Y16 )
{
	UniformLocationMapItem	key( fmt, argsX16Y16 );

	size_t	hashMask = uniLocationsMapMask;
	size_t	i = key.hashFunction() & hashMask;
	for ( ; uniLocationsMap[i].fmt; i = (i + 1) & hashMask ) {
		if ( uniLocationsMap[i] == key )
			return uniLocationsMap[i].l;
	}

	return storeUniformLocation( key, i );
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

void Renderer::Program::uni4f_l( int l, FloatVector4 x )
{
	f->glUniform4f( l, x[0], x[1], x[2], x[3] );
}

void Renderer::Program::uni4srgb_l( int l, FloatVector4 x )
{
	x = DDSTexture16::srgbExpand( x );
	f->glUniform4f( l, x[0], x[1], x[2], x[3] );
}

void Renderer::Program::uni4c_l( int l, std::uint32_t c, bool isSRGB )
{
	FloatVector4	x(c);
	x *= 1.0f / 255.0f;
	if ( isSRGB )
		x = DDSTexture16::srgbExpand( x );
	f->glUniform4f( l, x[0], x[1], x[2], x[3] );
}

void Renderer::Program::uni1bv_l( int l, const bool * x, size_t n )
{
	n = std::min< size_t >( n, 64 );
	GLint	tmp[64];
	for ( size_t i = 0; i < n; i++ )
		tmp[i] = GLint( x[i] );
	f->glUniform1iv( l, GLsizei(n), tmp );
}

void Renderer::Program::uni1iv_l( int l, const int * x, size_t n )
{
	f->glUniform1iv( l, GLsizei(n), x );
}

void Renderer::Program::uni1fv_l( int l, const float * x, size_t n )
{
	f->glUniform1fv( l, GLsizei(n), x );
}

void Renderer::Program::uni4fv_l( int l, const FloatVector4 * x, size_t n )
{
	f->glUniform4fv( l, GLsizei(n), &(x[0].v[0]) );
}

void Renderer::Program::uniSampler_l( int l, int firstTextureUnit, int textureCnt, int arraySize )
{
	arraySize = std::min< int >( arraySize, TexCache::maxTextureUnits );
	textureCnt = std::min< int >( textureCnt, arraySize );
	GLint	tmp[TexCache::maxTextureUnits];
	int	i;
	for ( i = 0; i < textureCnt; i++ )
		tmp[i] = firstTextureUnit + i;
	for ( ; i < arraySize; i++ )
		tmp[i] = firstTextureUnit;
	f->glUniform1iv( l, arraySize, tmp );
}

static int setFlipbookParameters( const CE2Material::Material & m )
{
	int	flipbookColumns = std::min< int >( m.flipbookColumns, 127 );
	int	flipbookRows = std::min< int >( m.flipbookRows, 127 );
	int	flipbookFrames = flipbookColumns * flipbookRows;
	if ( flipbookFrames < 2 )
		return 0;
	float	flipbookFPMS = std::min( std::max( m.flipbookFPS, 1.0f ), 100.0f ) * 0.001f;
	double	flipbookFrame = double( std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now().time_since_epoch() ).count() );
	flipbookFrame = flipbookFrame * flipbookFPMS / double( flipbookFrames );
	flipbookFrame = flipbookFrame - std::floor( flipbookFrame );
	int	materialFlags = ( flipbookColumns << 2 ) | ( flipbookRows << 9 );
	materialFlags = materialFlags | ( std::min< int >( int( flipbookFrame * double( flipbookFrames ) ), flipbookFrames - 1 ) << 16 );
	return materialFlags;
}

static inline void setupGLBlendModeSF( int blendMode, QOpenGLFunctions * fn )
{
	// source RGB, destination RGB, source alpha, destination alpha
	static const GLenum blendModeMap[32] = {
		GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA,	// AlphaBlend
		GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA,	// Additive
		GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA,	// SourceSoftAdditive (TODO: not implemented yet)
		GL_DST_COLOR, GL_ZERO, GL_DST_ALPHA, GL_ZERO,	// Multiply
		GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA,	// TODO: DestinationSoftAdditive
		GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA,	// TODO: DestinationInvertedSoftAdditive
		GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA,	// TODO: TakeSmaller
		GL_ZERO, GL_ONE, GL_ZERO, GL_ONE	// None
	};
	const GLenum *	p = &( blendModeMap[blendMode << 2] );
	glEnable( GL_BLEND );
	fn->glBlendFuncSeparate( p[0], p[1], p[2], p[3] );
}

bool Renderer::setupProgramCE2( const NifModel * nif, Program * prog, Shape * mesh )
{
	auto scene = mesh->scene;
	auto lsp = mesh->bslsp;
	if ( !lsp )
		return false;

	const CE2Material *	mat = nullptr;
	bool	useErrorColor = false;
	if ( !lsp->getSFMaterial( mat, nif ) )
		useErrorColor = scene->hasOption(Scene::DoErrorColor);
	if ( !mat )
		return false;

	mesh->depthWrite = true;
	mesh->depthTest = true;
	bool	isEffect = ( (mat->flags & CE2Material::Flag_IsEffect) && mat->shaderRoute != 0 );
	if ( isEffect ) {
		mesh->depthWrite = bool(mat->effectSettings->flags & CE2Material::EffectFlag_ZWrite);
		mesh->depthTest = bool(mat->effectSettings->flags & CE2Material::EffectFlag_ZTest);
	}

	// texturing

	BSShaderLightingProperty * bsprop = lsp;

	int texunit = 0;

	// Always bind cube to texture units 0 (specular) and 1 (diffuse),
	// regardless of shader settings
	bool hasCubeMap = scene->hasOption(Scene::DoCubeMapping) && scene->hasOption(Scene::DoLighting);
	GLint uniCubeMap = prog->uniformLocations[SAMP_CUBE];
	if ( uniCubeMap < 0 || !activateTextureUnit( texunit ) )
		return false;
	hasCubeMap = hasCubeMap && bsprop->bindCube( cfg.cubeMapPathSTF );
	if ( !hasCubeMap ) [[unlikely]]
		bsprop->bind( gray, true );
	fn->glUniform1i( uniCubeMap, texunit++ );

	uniCubeMap = prog->uniformLocations[SAMP_CUBE_2];
	if ( uniCubeMap < 0 || !activateTextureUnit( texunit ) )
		return false;
	hasCubeMap = hasCubeMap && bsprop->bindCube( cfg.cubeMapPathSTF, true );
	if ( !hasCubeMap ) [[unlikely]]
		bsprop->bind( gray, true );
	fn->glUniform1i( uniCubeMap, texunit++ );

	prog->uni1i( HAS_MAP_CUBE, hasCubeMap );

	// texture unit 2 is reserved for the environment BRDF LUT texture
	if ( !activateTextureUnit( texunit, true ) )
		return false;
	if ( !bsprop->bind( pbr_lut_sf, true, TexClampMode::CLAMP_S_CLAMP_T ) )
		return false;
	texunit++;

	CE2Material::UVStream	defaultUVStream;
	defaultUVStream.scaleAndOffset = FloatVector4( 1.0f, 1.0f, 0.0f, 0.0f );
	defaultUVStream.textureAddressMode = 0;	// "Wrap"
	defaultUVStream.channel = 1;	// "One"

	prog->uni1b_l( prog->uniLocation("isWireframe"), false );
	prog->uni1i( HAS_SPECULAR, int(scene->hasOption(Scene::DoSpecular)) );
	prog->uni1i_l( prog->uniLocation("lm.shaderModel"), mat->shaderModel );
	prog->uni4f_l( prog->uniLocation("parallaxOcclusionSettings"), FloatVector4( 8.0f, float(cfg.sfParallaxMaxSteps), cfg.sfParallaxScale, cfg.sfParallaxOffset ) );

	// emissive settings
	if ( mat->flags & CE2Material::Flag_LayeredEmissivity && scene->hasOption(Scene::DoGlow) ) {
		const CE2Material::LayeredEmissiveSettings *	sp = mat->layeredEmissiveSettings;
		prog->uni1b_l( prog->uniLocation("lm.layeredEmissivity.isEnabled"), sp->isEnabled );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.firstLayerIndex"), sp->layer1Index );
		prog->uni4c_l( prog->uniLocation("lm.layeredEmissivity.firstLayerTint"), sp->layer1Tint, true );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.firstLayerMaskIndex"), sp->layer1MaskIndex );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.secondLayerIndex"), ( sp->layer2Active ? int(sp->layer2Index) : -1 ) );
		prog->uni4c_l( prog->uniLocation("lm.layeredEmissivity.secondLayerTint"), sp->layer2Tint, true );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.secondLayerMaskIndex"), sp->layer2MaskIndex );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.firstBlenderIndex"), sp->blender1Index );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.firstBlenderMode"), sp->blender1Mode );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.thirdLayerIndex"), ( sp->layer3Active ? int(sp->layer3Index) : -1 ) );
		prog->uni4c_l( prog->uniLocation("lm.layeredEmissivity.thirdLayerTint"), sp->layer3Tint, true );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.thirdLayerMaskIndex"), sp->layer3MaskIndex );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.secondBlenderIndex"), sp->blender2Index );
		prog->uni1i_l( prog->uniLocation("lm.layeredEmissivity.secondBlenderMode"), sp->blender2Mode );
		prog->uni1f_l( prog->uniLocation("lm.layeredEmissivity.emissiveClipThreshold"), sp->clipThreshold );
		prog->uni1b_l( prog->uniLocation("lm.layeredEmissivity.adaptiveEmittance"), sp->adaptiveEmittance );
		prog->uni1f_l( prog->uniLocation("lm.layeredEmissivity.luminousEmittance"), sp->luminousEmittance );
		prog->uni1f_l( prog->uniLocation("lm.layeredEmissivity.exposureOffset"), sp->exposureOffset );
		prog->uni1b_l( prog->uniLocation("lm.layeredEmissivity.enableAdaptiveLimits"), sp->enableAdaptiveLimits );
		prog->uni1f_l( prog->uniLocation("lm.layeredEmissivity.maxOffsetEmittance"), sp->maxOffset );
		prog->uni1f_l( prog->uniLocation("lm.layeredEmissivity.minOffsetEmittance"), sp->minOffset );
	}	else {
		prog->uni1b_l( prog->uniLocation("lm.layeredEmissivity.isEnabled"), false );
	}
	if ( mat->flags & CE2Material::Flag_Emissive && scene->hasOption(Scene::DoGlow) ) {
		const CE2Material::EmissiveSettings *	sp = mat->emissiveSettings;
		prog->uni1b_l( prog->uniLocation("lm.emissiveSettings.isEnabled"), sp->isEnabled );
		prog->uni1i_l( prog->uniLocation("lm.emissiveSettings.emissiveSourceLayer"), sp->sourceLayer );
		prog->uni4srgb_l( prog->uniLocation("lm.emissiveSettings.emissiveTint"), sp->emissiveTint );
		prog->uni1i_l( prog->uniLocation("lm.emissiveSettings.emissiveMaskSourceBlender"), sp->maskSourceBlender );
		prog->uni1f_l( prog->uniLocation("lm.emissiveSettings.emissiveClipThreshold"), sp->clipThreshold );
		prog->uni1b_l( prog->uniLocation("lm.emissiveSettings.adaptiveEmittance"), sp->adaptiveEmittance );
		prog->uni1f_l( prog->uniLocation("lm.emissiveSettings.luminousEmittance"), sp->luminousEmittance );
		prog->uni1f_l( prog->uniLocation("lm.emissiveSettings.exposureOffset"), sp->exposureOffset );
		prog->uni1b_l( prog->uniLocation("lm.emissiveSettings.enableAdaptiveLimits"), sp->enableAdaptiveLimits );
		prog->uni1f_l( prog->uniLocation("lm.emissiveSettings.maxOffsetEmittance"), sp->maxOffset );
		prog->uni1f_l( prog->uniLocation("lm.emissiveSettings.minOffsetEmittance"), sp->minOffset );
	}	else {
		prog->uni1b_l( prog->uniLocation("lm.emissiveSettings.isEnabled"), false );
	}

	// translucency settings
	if ( mat->flags & CE2Material::Flag_Translucency ) {
		const CE2Material::TranslucencySettings *	sp = mat->translucencySettings;
		prog->uni1b_l( prog->uniLocation("lm.translucencySettings.isEnabled"), sp->isEnabled );
		prog->uni1b_l( prog->uniLocation("lm.translucencySettings.isThin"), sp->isThin );
		prog->uni1b_l( prog->uniLocation("lm.translucencySettings.flipBackFaceNormalsInViewSpace"), sp->flipBackFaceNormalsInVS );
		prog->uni1b_l( prog->uniLocation("lm.translucencySettings.useSSS"), sp->useSSS );
		prog->uni1f_l( prog->uniLocation("lm.translucencySettings.sssWidth"), sp->sssWidth );
		prog->uni1f_l( prog->uniLocation("lm.translucencySettings.sssStrength"), sp->sssStrength );
		prog->uni1f_l( prog->uniLocation("lm.translucencySettings.transmissiveScale"), sp->transmissiveScale );
		prog->uni1f_l( prog->uniLocation("lm.translucencySettings.transmittanceWidth"), sp->transmittanceWidth );
		prog->uni1f_l( prog->uniLocation("lm.translucencySettings.specLobe0RoughnessScale"), sp->specLobe0RoughnessScale );
		prog->uni1f_l( prog->uniLocation("lm.translucencySettings.specLobe1RoughnessScale"), sp->specLobe1RoughnessScale );
		prog->uni1i_l( prog->uniLocation("lm.translucencySettings.transmittanceSourceLayer"), sp->sourceLayer );
	} else {
		prog->uni1b_l( prog->uniLocation("lm.translucencySettings.isEnabled"), false );
	}

	// decal settings
	if ( mat->flags & CE2Material::Flag_IsDecal ) {
		const CE2Material::DecalSettings *	sp = mat->decalSettings;
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.isDecal"), sp->isDecal );
		prog->uni1f_l( prog->uniLocation("lm.decalSettings.materialOverallAlpha"), sp->decalAlpha );
		prog->uni1i_l( prog->uniLocation("lm.decalSettings.writeMask"), int(sp->writeMask) );
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.isPlanet"), sp->isPlanet );
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.isProjected"), sp->isProjected );
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.useParallaxOcclusionMapping"), sp->useParallaxMapping );
		int	texUniform = 0;
		bsprop->getSFTexture( texunit, texUniform, nullptr, sp->surfaceHeightMap, 0, 0, nullptr );
		prog->uni1i_l( prog->uniLocation("lm.decalSettings.surfaceHeightMap"), texUniform );
		prog->uni1f_l( prog->uniLocation("lm.decalSettings.parallaxOcclusionScale"), sp->parallaxOcclusionScale );
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.parallaxOcclusionShadows"), sp->parallaxOcclusionShadows );
		prog->uni1i_l( prog->uniLocation("lm.decalSettings.maxParralaxOcclusionSteps"), sp->maxParallaxSteps );
		prog->uni1i_l( prog->uniLocation("lm.decalSettings.renderLayer"), sp->renderLayer );
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.useGBufferNormals"), sp->useGBufferNormals );
		prog->uni1i_l( prog->uniLocation("lm.decalSettings.blendMode"), sp->blendMode );
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.animatedDecalIgnoresTAA"), sp->animatedDecalIgnoresTAA );
	} else {
		prog->uni1b_l( prog->uniLocation("lm.decalSettings.isDecal"), false );
	}

	// effect settings
	prog->uni1b_l( prog->uniLocation("lm.isEffect"), isEffect );
	prog->uni1b_l( prog->uniLocation("lm.hasOpacityComponent"), ( isEffect && (mat->flags & CE2Material::Flag_HasOpacityComponent) ) );
	int	layeredEdgeFalloffFlags = 0;
	if ( isEffect ) {
		const CE2Material::EffectSettings *	sp = mat->effectSettings;
		if ( mat->flags & CE2Material::Flag_LayeredEdgeFalloff )
			layeredEdgeFalloffFlags = mat->layeredEdgeFalloff->activeLayersMask & 0x07;
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.vertexColorBlend"), bool(sp->flags & CE2Material::EffectFlag_VertexColorBlend) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.isAlphaTested"), bool(sp->flags & CE2Material::EffectFlag_IsAlphaTested) );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.alphaTestThreshold"), sp->alphaThreshold );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.noHalfResOptimization"), bool(sp->flags & CE2Material::EffectFlag_NoHalfResOpt) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.softEffect"), bool(sp->flags & CE2Material::EffectFlag_SoftEffect) );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.softFalloffDepth"), sp->softFalloffDepth );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.emissiveOnlyEffect"), bool(sp->flags & CE2Material::EffectFlag_EmissiveOnly) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.emissiveOnlyAutomaticallyApplied"), bool(sp->flags & CE2Material::EffectFlag_EmissiveOnlyAuto) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.receiveDirectionalShadows"), bool(sp->flags & CE2Material::EffectFlag_DirShadows) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.receiveNonDirectionalShadows"), bool(sp->flags & CE2Material::EffectFlag_NonDirShadows) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.isGlass"), bool(sp->flags & CE2Material::EffectFlag_IsGlass) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.frosting"), bool(sp->flags & CE2Material::EffectFlag_Frosting) );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.frostingUnblurredBackgroundAlphaBlend"), sp->frostingBgndBlend );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.frostingBlurBias"), sp->frostingBlurBias );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.materialOverallAlpha"), sp->materialAlpha );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.zTest"), bool(sp->flags & CE2Material::EffectFlag_ZTest) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.zWrite"), bool(sp->flags & CE2Material::EffectFlag_ZWrite) );
		prog->uni1i_l( prog->uniLocation("lm.effectSettings.blendingMode"), sp->blendMode );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.backLightingEnable"), bool(sp->flags & CE2Material::EffectFlag_BacklightEnable) );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.backlightingScale"), sp->backlightScale );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.backlightingSharpness"), sp->backlightSharpness );
		prog->uni1f_l( prog->uniLocation("lm.effectSettings.backlightingTransparencyFactor"), sp->backlightTransparency );
		prog->uni4f_l( prog->uniLocation("lm.effectSettings.backLightingTintColor"), sp->backlightTintColor );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.depthMVFixup"), bool(sp->flags & CE2Material::EffectFlag_MVFixup) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.depthMVFixupEdgesOnly"), bool(sp->flags & CE2Material::EffectFlag_MVFixupEdgesOnly) );
		prog->uni1b_l( prog->uniLocation("lm.effectSettings.forceRenderBeforeOIT"), bool(sp->flags & CE2Material::EffectFlag_RenderBeforeOIT) );
		prog->uni1i_l( prog->uniLocation("lm.effectSettings.depthBiasInUlp"), sp->depthBias );
		// opacity component
		if ( mat->flags & CE2Material::Flag_HasOpacityComponent ) {
			prog->uni1i_l( prog->uniLocation("lm.opacity.firstLayerIndex"), mat->opacityLayer1 );
			prog->uni1b_l( prog->uniLocation("lm.opacity.secondLayerActive"), bool(mat->flags & CE2Material::Flag_OpacityLayer2Active) );
			if ( mat->flags & CE2Material::Flag_OpacityLayer2Active ) {
				prog->uni1i_l( prog->uniLocation("lm.opacity.secondLayerIndex"), mat->opacityLayer2 );
				prog->uni1i_l( prog->uniLocation("lm.opacity.firstBlenderIndex"), mat->opacityBlender1 );
				prog->uni1i_l( prog->uniLocation("lm.opacity.firstBlenderMode"), mat->opacityBlender1Mode );
			}
			prog->uni1b_l( prog->uniLocation("lm.opacity.thirdLayerActive"), bool(mat->flags & CE2Material::Flag_OpacityLayer3Active) );
			if ( mat->flags & CE2Material::Flag_OpacityLayer3Active ) {
				prog->uni1i_l( prog->uniLocation("lm.opacity.thirdLayerIndex"), mat->opacityLayer3 );
				prog->uni1i_l( prog->uniLocation("lm.opacity.secondBlenderIndex"), mat->opacityBlender2 );
				prog->uni1i_l( prog->uniLocation("lm.opacity.secondBlenderMode"), mat->opacityBlender2Mode );
			}
			prog->uni1f_l( prog->uniLocation("lm.opacity.specularOpacityOverride"), mat->specularOpacityOverride );
		}
	}
	if ( layeredEdgeFalloffFlags ) {
		const CE2Material::LayeredEdgeFalloff *	sp = mat->layeredEdgeFalloff;
		prog->uni1fv_l( prog->uniLocation("lm.layeredEdgeFalloff.falloffStartAngles"), sp->falloffStartAngles, 3 );
		prog->uni1fv_l( prog->uniLocation("lm.layeredEdgeFalloff.falloffStopAngles"), sp->falloffStopAngles, 3 );
		prog->uni1fv_l( prog->uniLocation("lm.layeredEdgeFalloff.falloffStartOpacities"), sp->falloffStartOpacities, 3 );
		prog->uni1fv_l( prog->uniLocation("lm.layeredEdgeFalloff.falloffStopOpacities"), sp->falloffStopOpacities, 3 );
		if ( sp->useRGBFalloff )
			layeredEdgeFalloffFlags = layeredEdgeFalloffFlags | 0x80;
	}
	prog->uni1i_l( prog->uniLocation("lm.layeredEdgeFalloff.flags"), layeredEdgeFalloffFlags );

	// alpha settings
	if ( mat->flags & CE2Material::Flag_HasOpacity ) {
		prog->uni1b_l( prog->uniLocation("lm.alphaSettings.hasOpacity"), true );
		prog->uni1f_l( prog->uniLocation("lm.alphaSettings.alphaTestThreshold"), mat->alphaThreshold );
		prog->uni1i_l( prog->uniLocation("lm.alphaSettings.opacitySourceLayer"), mat->alphaSourceLayer );
		prog->uni1i_l( prog->uniLocation("lm.alphaSettings.alphaBlenderMode"), mat->alphaBlendMode );
		prog->uni1b_l( prog->uniLocation("lm.alphaSettings.useDetailBlendMask"), bool(mat->flags & CE2Material::Flag_AlphaDetailBlendMask) );
		prog->uni1b_l( prog->uniLocation("lm.alphaSettings.useVertexColor"), bool(mat->flags & CE2Material::Flag_AlphaVertexColor) );
		prog->uni1i_l( prog->uniLocation("lm.alphaSettings.vertexColorChannel"), mat->alphaVertexColorChannel );
		const CE2Material::UVStream *	uvStream = mat->alphaUVStream;
		if ( !uvStream )
			uvStream = &defaultUVStream;
		prog->uni4f_l( prog->uniLocation("lm.alphaSettings.opacityUVstream.scaleAndOffset"), uvStream->scaleAndOffset );
		prog->uni1b_l( prog->uniLocation("lm.alphaSettings.opacityUVstream.useChannelTwo"), (uvStream->channel > 1) );
		prog->uni1f_l( prog->uniLocation("lm.alphaSettings.heightBlendThreshold"), mat->alphaHeightBlendThreshold );
		prog->uni1f_l( prog->uniLocation("lm.alphaSettings.heightBlendFactor"), mat->alphaHeightBlendFactor );
		prog->uni1f_l( prog->uniLocation("lm.alphaSettings.position"), mat->alphaPosition );
		prog->uni1f_l( prog->uniLocation("lm.alphaSettings.contrast"), mat->alphaContrast );
		prog->uni1b_l( prog->uniLocation("lm.alphaSettings.useDitheredTransparency"), bool(mat->flags & CE2Material::Flag_DitheredTransparency) );
	} else {
		prog->uni1b_l( prog->uniLocation("lm.alphaSettings.hasOpacity"), false );
	}

	// detail blender settings
	if ( ( mat->flags & CE2Material::Flag_UseDetailBlender ) && mat->detailBlenderSettings->isEnabled ) {
		const CE2Material::DetailBlenderSettings *	sp = mat->detailBlenderSettings;
		prog->uni1b_l( prog->uniLocation("lm.detailBlender.detailBlendMaskSupported"), true );
		const CE2Material::UVStream *	uvStream = sp->uvStream;
		if ( !uvStream )
			uvStream = &defaultUVStream;
		int	texUniform = 0;
		FloatVector4	replUniform( 0.0f );
		bsprop->getSFTexture( texunit, texUniform, &replUniform, sp->texturePath, sp->textureReplacement, int(sp->textureReplacementEnabled), uvStream );
		prog->uni1i_l( prog->uniLocation("lm.detailBlender.maskTexture"), texUniform );
		if ( texUniform < 0 )
			prog->uni4f_l( prog->uniLocation("lm.detailBlender.maskTextureReplacement"), replUniform );
		prog->uni4f_l( prog->uniLocation("lm.detailBlender.uvStream.scaleAndOffset"), uvStream->scaleAndOffset );
		prog->uni1b_l( prog->uniLocation("lm.detailBlender.uvStream.useChannelTwo"), (uvStream->channel > 1) );
	} else {
		prog->uni1b_l( prog->uniLocation("lm.detailBlender.detailBlendMaskSupported"), false );
	}

	// material layers
	int	texUniforms[11];
	FloatVector4	replUniforms[11];
	std::uint32_t	layerMask = mat->layerMask & 0x0F;	// limit the number of layers to 4
	for ( int i = 0; i < 4; i++ )
		texUniforms[i] = int( (layerMask >> i) & 1 );
	prog->uni1iv_l( prog->uniLocation("lm.layersEnabled"), texUniforms, 4 );
	for ( int i = 0; layerMask; i++, layerMask = layerMask >> 1 ) {
		if ( !( layerMask & 1 ) )
			continue;
		const CE2Material::Layer *	layer = mat->layers[i];
		if ( layer->material ) {
			prog->uni4srgb_l( prog->uniLocation("lm.layers[%d].material.color", i), layer->material->color );
			int	materialFlags = layer->material->colorModeFlags & 3;
			if ( layer->material->flipbookFlags & 1 ) [[unlikely]]
				materialFlags = materialFlags | setFlipbookParameters( *(layer->material) );
			prog->uni1i_l( prog->uniLocation("lm.layers[%d].material.flags", i), materialFlags );
		} else {
			prog->uni4f_l( prog->uniLocation("lm.layers[%d].material.color", i), FloatVector4(1.0f) );
			prog->uni1i_l( prog->uniLocation("lm.layers[%d].material.flags", i), 0 );
		}
		for ( int j = 0; j < 11; j++ ) {
			texUniforms[j] = 0;
			replUniforms[j] = FloatVector4( 0.0f );
		}
		if ( layer->material && layer->material->textureSet ) {
			const CE2Material::TextureSet *	textureSet = layer->material->textureSet;
			prog->uni1f_l( prog->uniLocation("lm.layers[%d].material.textureSet.floatParam", i), textureSet->floatParam );
			for ( int j = 0; j < 11 && j < CE2Material::TextureSet::maxTexturePaths; j++ ) {
				const std::string_view *	texturePath = nullptr;
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
				if ( j == 2 && i == mat->alphaSourceLayer )
					uvStream = mat->alphaUVStream;
				bsprop->getSFTexture( texunit, texUniforms[j], &(replUniforms[j]), texturePath, textureReplacement, textureReplacementMode, uvStream );
			}
		} else {
			prog->uni1f_l( prog->uniLocation("lm.layers[%d].material.textureSet.floatParam", i), 1.0f );
			for ( int j = 0; j < 11 && j < CE2Material::TextureSet::maxTexturePaths; j++ ) {
				bsprop->getSFTexture( texunit, texUniforms[j], &(replUniforms[j]), nullptr, defaultSFTextureSet[j], int(j < 6), layer->uvStream );
			}
		}
		prog->uni1iv_l( prog->uniLocation("lm.layers[%d].material.textureSet.textures", i), texUniforms, 11 );
		prog->uni4fv_l( prog->uniLocation("lm.layers[%d].material.textureSet.textureReplacements", i), replUniforms, 11 );
		const CE2Material::UVStream *	uvStream = layer->uvStream;
		if ( !uvStream )
			uvStream = &defaultUVStream;
		prog->uni4f_l( prog->uniLocation("lm.layers[%d].uvStream.scaleAndOffset", i), uvStream->scaleAndOffset );
		prog->uni1b_l( prog->uniLocation("lm.layers[%d].uvStream.useChannelTwo", i), (uvStream->channel > 1) );

		const CE2Material::Blender *	blender;
		if ( !( i > 0 && i <= CE2Material::maxBlenders && ( blender = mat->blenders[i - 1] ) != nullptr ) )
			continue;
		uvStream = blender->uvStream;
		if ( !uvStream )
			uvStream = &defaultUVStream;
		prog->uni4f_l( prog->uniLocation("lm.blenders[%d].uvStream.scaleAndOffset", i - 1), uvStream->scaleAndOffset );
		prog->uni1b_l( prog->uniLocation("lm.blenders[%d].uvStream.useChannelTwo", i - 1), (uvStream->channel > 1) );
		int	texUniform = 0;
		FloatVector4	replUniform( 0.0f );
		bsprop->getSFTexture( texunit, texUniform, &replUniform, blender->texturePath, blender->textureReplacement, int(blender->textureReplacementEnabled), uvStream );
		prog->uni1i_l( prog->uniLocation("lm.blenders[%d].maskTexture", i - 1), texUniform );
		if ( texUniform < 0 )
			prog->uni4f_l( prog->uniLocation("lm.blenders[%d].maskTextureReplacement", i - 1), replUniform );
		prog->uni1i_l( prog->uniLocation("lm.blenders[%d].blendMode", i - 1), int(blender->blendMode) );
		prog->uni1i_l( prog->uniLocation("lm.blenders[%d].colorChannel", i - 1), int(blender->colorChannel) );
		prog->uni1fv_l( prog->uniLocation("lm.blenders[%d].floatParams", i - 1), blender->floatParams, CE2Material::Blender::maxFloatParams );
		prog->uni1bv_l( prog->uniLocation("lm.blenders[%d].boolParams", i - 1), blender->boolParams, CE2Material::Blender::maxBoolParams );
	}

	prog->uniSampler_l( prog->uniLocation("textureUnits"), 2, texunit - 2, TexCache::num_texture_units - 2 );

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
		} else {
			int txid = it;
			if ( txid < 0 )
				return false;

			if ( typeid(*mesh) != typeid(BSMesh) )
				return false;
			const MeshFile *	sfMesh = static_cast< BSMesh * >(mesh)->getMeshFile();
			if ( !sfMesh || sfMesh->coords.count() != sfMesh->positions.count() )
				return false;

			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			glTexCoordPointer( 4, GL_FLOAT, 0, sfMesh->coords.constData() );
		}
	}

	// setup lighting

	//glEnable( GL_LIGHTING );

	// setup alpha blending and testing

	int	alphaFlags = 0;
	if ( mat && scene->hasOption(Scene::DoBlending) ) {
		if ( isEffect || !( ~(mat->flags) & ( CE2Material::Flag_IsDecal | CE2Material::Flag_AlphaBlending ) ) ) {
			int	blendMode;
			if ( !isEffect ) {
				blendMode = mat->decalSettings->blendMode;
			} else if ( !( mat->effectSettings->flags & (CE2Material::EffectFlag_EmissiveOnly | CE2Material::EffectFlag_EmissiveOnlyAuto) ) ) {
				blendMode = mat->effectSettings->blendMode;
			} else {
				blendMode = 1;	// emissive only: additive blending
			}
			setupGLBlendModeSF( blendMode, prog->f );
			alphaFlags = 2;
		}

		if ( isEffect )
			alphaFlags |= int( bool(mat->effectSettings->flags & CE2Material::EffectFlag_IsAlphaTested) );
		else
			alphaFlags |= int( bool(mat->flags & CE2Material::Flag_HasOpacity) && mat->alphaThreshold > 0.0f );

		if ( mat->flags & CE2Material::Flag_IsDecal ) {
			glEnable( GL_POLYGON_OFFSET_FILL );
			glPolygonOffset( -1.0f, -1.0f );
		}
	}
	prog->uni1i_l( prog->uniLocation("alphaFlags"), alphaFlags );
	if ( !( alphaFlags & 2 ) )
		glDisable( GL_BLEND );
	glDisable( GL_ALPHA_TEST );

	glDisable( GL_COLOR_MATERIAL );
	if ( !mesh->depthTest ) [[unlikely]]
		glDisable( GL_DEPTH_TEST );
	else
		glEnable( GL_DEPTH_TEST );
	glDepthMask( !mesh->depthWrite || mesh->translucent ? GL_FALSE : GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	if ( mat->flags & CE2Material::Flag_TwoSided ) {
		glDisable( GL_CULL_FACE );
	} else {
		glEnable( GL_CULL_FACE );
		glCullFace( GL_BACK );
	}
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

	return true;
}

bool Renderer::setupProgramCE1( const NifModel * nif, Program * prog, Shape * mesh )
{
	auto nifVersion = nif->getBSVersion();
	auto scene = mesh->scene;
	auto lsp = mesh->bslsp;
	auto esp = mesh->bsesp;

	BSShaderLightingProperty * bsprop;
	if ( lsp )
		bsprop = lsp;
	else if ( esp )
		bsprop = esp;
	else
		return false;
	Material * mat = bsprop->getMaterial();

	const QString & default_n = (nifVersion >= 151) ? ::default_ns : ::default_n;

	// texturing

	TexClampMode clamp = bsprop->clampMode;

	QString	emptyString;
	int texunit = 0;
	if ( lsp ) {
		// BSLightingShaderProperty

		const QString *	forced = &emptyString;
		if ( scene->hasOption(Scene::DoLighting) && scene->hasVisMode(Scene::VisNormalsOnly) )
			forced = &white;
		const QString &	alt = ( !scene->hasOption(Scene::DoErrorColor) ? white : magenta );
		prog->uniSampler( bsprop, SAMP_BASE, 0, texunit, alt, clamp, *forced );

		forced = &emptyString;
		if ( !scene->hasOption(Scene::DoLighting) )
			forced = &default_n;
		prog->uniSampler( lsp, SAMP_NORMAL, 1, texunit, emptyString, clamp, *forced );

		prog->uniSampler( lsp, SAMP_GLOW, 2, texunit, black, clamp );

		prog->uni1f( LIGHT_EFF1, lsp->lightingEffect1 );
		prog->uni1f( LIGHT_EFF2, lsp->lightingEffect2 );

		prog->uni1f( ALPHA, lsp->alpha );

		prog->uni2f( UV_SCALE, lsp->uvScale.x, lsp->uvScale.y );
		prog->uni2f( UV_OFFSET, lsp->uvOffset.x, lsp->uvOffset.y );

		prog->uni4m( MAT_VIEW, mesh->viewTrans().toMatrix4() );

		prog->uni1i( G2P_COLOR, lsp->greyscaleColor );
		prog->uniSampler( bsprop, SAMP_GRAYSCALE, 3, texunit, "", TexClampMode::CLAMP_S_CLAMP_T );

		prog->uni1i( HAS_TINT_COLOR, lsp->hasTintColor );
		if ( lsp->hasTintColor ) {
			prog->uni3f( TINT_COLOR, lsp->tintColor.red(), lsp->tintColor.green(), lsp->tintColor.blue() );
		}

		prog->uni1i( HAS_MAP_DETAIL, lsp->hasDetailMask );
		prog->uniSampler( bsprop, SAMP_DETAIL, 3, texunit, "#FF404040", clamp );

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

		if ( nifVersion >= 151 )
			prog->uni1i( HAS_SPECULAR, int(scene->hasOption(Scene::DoSpecular)) );
		else		// Assure specular power does not break the shaders
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

		bool	hasCubeMap = ( scene->hasOption(Scene::DoCubeMapping) && scene->hasOption(Scene::DoLighting) && (lsp->hasEnvironmentMap || nifVersion >= 151) );
		prog->uni1i( HAS_MASK_ENV, lsp->useEnvironmentMask );
		float refl = ( nifVersion < 151 ? lsp->environmentReflection : 1.0f );
		prog->uni1f( ENV_REFLECTION, refl );

		// Always bind cube regardless of shader settings
		GLint uniCubeMap = prog->uniformLocations[SAMP_CUBE];
		if ( uniCubeMap < 0 ) {
			hasCubeMap = false;
		} else {
			if ( !activateTextureUnit( texunit ) )
				return false;
			QString	fname = bsprop->fileName( 4 );
			const QString *	cube = &fname;
			if ( hasCubeMap && ( fname.isEmpty() || !bsprop->bindCube( fname ) ) ) {
				cube = ( nifVersion < 151 ? ( nifVersion < 128 ? &cube_sk : &cube_fo4 ) : &cfg.cubeMapPathFO76 );
				hasCubeMap = bsprop->bindCube( *cube );
			}
			if ( !hasCubeMap ) [[unlikely]]
				bsprop->bind( gray, true );
			fn->glUniform1i( uniCubeMap, texunit++ );
			if ( nifVersion >= 151 && ( uniCubeMap = prog->uniformLocations[SAMP_CUBE_2] ) >= 0 ) {
				// Fallout 76: load second cube map for diffuse lighting
				if ( !activateTextureUnit( texunit ) )
					return false;
				hasCubeMap = hasCubeMap && bsprop->bindCube( *cube, true );
				if ( !hasCubeMap ) [[unlikely]]
					bsprop->bind( gray, true );
				fn->glUniform1i( uniCubeMap, texunit++ );
			}
		}
		prog->uni1i( HAS_MAP_CUBE, hasCubeMap );

		if ( nifVersion < 151 ) {
			// Always bind mask regardless of shader settings
			prog->uniSampler( bsprop, SAMP_ENV_MASK, 5, texunit, white, clamp );
		} else {
			if ( prog->uniformLocations[SAMP_ENV_MASK] >= 0 ) {
				if ( !activateTextureUnit( texunit ) )
					return false;
				if ( !bsprop->bind( pbr_lut_sf, true, TexClampMode::CLAMP_S_CLAMP_T ) )
					return false;
				fn->glUniform1i( prog->uniformLocations[SAMP_ENV_MASK], texunit++ );
			}
			prog->uniSampler( bsprop, SAMP_REFLECTIVITY, 8, texunit, reflectivity, clamp );
			prog->uniSampler( bsprop, SAMP_LIGHTING, 9, texunit, lighting, clamp );
		}

		// Parallax
		prog->uni1i( HAS_MAP_HEIGHT, lsp->hasHeightMap );
		prog->uniSampler( bsprop, SAMP_HEIGHT, 3, texunit, gray, clamp );

	} else {
		// BSEffectShaderProperty

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

		// BSEffectShader textures (FIXME: should implement using error color?)

		prog->uniSampler( bsprop, SAMP_BASE, 0, texunit, white, clamp );
		prog->uniSampler( bsprop, SAMP_GRAYSCALE, 1, texunit, "", TexClampMode::CLAMP_S_CLAMP_T );

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
				const QString&	cube = (nifVersion < 151 ? (nifVersion < 128 ? cube_sk : cube_fo4) : cfg.cubeMapPathFO76);
				if ( fname.isEmpty() )
					fname = cube;

				if ( !activateTextureUnit( texunit ) || !bsprop->bindCube( fname ) )
					if ( !activateTextureUnit( texunit ) || !bsprop->bindCube( cube ) )
						return false;


				fn->glUniform1i( uniCubeMap, texunit++ );
			}
			if ( nifVersion < 151 ) {
				prog->uniSampler( bsprop, SAMP_SPECULAR, 4, texunit, white, clamp );
			} else {
				prog->uniSampler( bsprop, SAMP_ENV_MASK, 4, texunit, white, clamp );
				prog->uniSampler( bsprop, SAMP_REFLECTIVITY, 6, texunit, reflectivity, clamp );
				prog->uniSampler( bsprop, SAMP_LIGHTING, 7, texunit, lighting, clamp );
				prog->uni1i( HAS_MAP_SPEC, int(!bsprop->fileName( 7 ).isEmpty()) );
			}

			prog->uni1f( LUM_EMIT, esp->lumEmittance );
		}
	}

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
		} else {
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

	if ( mesh->isDoubleSided ) {
		glDisable( GL_CULL_FACE );
	} else {
		glEnable( GL_CULL_FACE );
		glCullFace( GL_BACK );
	}

	// setup lighting

	//glEnable( GL_LIGHTING );

	// setup blending

	if ( mat ) {
		static const GLenum blendMap[11] = {
			GL_ONE, GL_ZERO, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
			GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA_SATURATE
		};

		if ( mat->hasAlphaBlend() && scene->hasOption(Scene::DoBlending) ) {
			glEnable( GL_BLEND );
			glBlendFunc( blendMap[mat->iAlphaSrc], blendMap[mat->iAlphaDst] );
		} else {
			glDisable( GL_BLEND );
		}

		if ( mat->hasAlphaTest() && scene->hasOption(Scene::DoBlending) ) {
			glEnable( GL_ALPHA_TEST );
			glAlphaFunc( GL_GREATER, float( mat->iAlphaTestRef ) / 255.0 );
		} else {
			glDisable( GL_ALPHA_TEST );
		}

		if ( mat->bDecal ) {
			glEnable( GL_POLYGON_OFFSET_FILL );
			glPolygonOffset( -1.0f, -1.0f );
		}

	} else {
		glProperty( mesh->alphaProperty );
		// BSESP/BSLSP do not always need an NiAlphaProperty, and appear to override it at times
		if ( mesh->translucent ) {
			glEnable( GL_BLEND );
			glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
			// If mesh is alpha tested, override threshold
			glAlphaFunc( GL_GREATER, 0.1f );
		}
	}

	glDisable( GL_COLOR_MATERIAL );

	if ( !mesh->depthTest ) {
		glDisable( GL_DEPTH_TEST );
	} else {
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LEQUAL );
	}
	glDepthMask( !mesh->depthWrite || mesh->translucent ? GL_FALSE : GL_TRUE );
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

	return true;
}

bool Renderer::setupProgramFO3( const NifModel * nif, Program * prog, Shape * mesh )
{
	auto scene = mesh->scene;
	auto esp = mesh->bsesp;

	// defaults for uniforms
	int	vertexColorFlags = 40;	// LIGHT_MODE_EMI_AMB_DIF + VERT_MODE_SRC_AMB_DIF
	bool	isDecal = false;
	bool	hasSpecular = ( scene->hasOption( Scene::DoSpecular ) && scene->hasOption( Scene::DoLighting ) );
	bool	hasEmit = false;
	bool	hasGlowMap = false;
	bool	hasCubeMap = ( scene->hasOption( Scene::DoCubeMapping ) && scene->hasOption( Scene::DoLighting ) );
	bool	hasCubeMask = false;
	float	cubeMapScale = 1.0f;
	int	parallaxMaxSteps = 0;
	float	parallaxScale = 0.03f;
	float	glowMult = ( scene->hasOption( Scene::DoGlow ) && scene->hasOption( Scene::DoLighting ) ? 1.0f : 0.0f );
	FloatVector4	glowColor( 1.0f );
	FloatVector4	uvScaleAndOffset( 1.0f, 1.0f, 0.0f, 0.0f );
	FloatVector4	uvCenterAndRotation( 0.5f, 0.5f, 0.0f, 0.0f );
	FloatVector4	falloffParams( 1.0f, 0.0f, 1.0f, 1.0f );

	// texturing

	TexturingProperty * texprop = mesh->findProperty< TexturingProperty >();
	BSShaderLightingProperty * bsprop = mesh->bssp;
	if ( !bsprop && !texprop )
		return false;

	TexClampMode clamp = TexClampMode::WRAP_S_WRAP_T;

	QString	emptyString;
	int texunit = 0;
	if ( bsprop ) {
		clamp = bsprop->clampMode;
		mesh->depthTest = bsprop->depthTest;
		mesh->depthWrite = bsprop->depthWrite;
		const QString *	forced = &emptyString;
		if ( scene->hasOption(Scene::DoLighting) && scene->hasVisMode(Scene::VisNormalsOnly) )
			forced = &white;

		const QString &	alt = ( !scene->hasOption(Scene::DoErrorColor) ? white : magenta );

		prog->uniSampler( bsprop, SAMP_BASE, 0, texunit, alt, clamp, *forced );
	} else {
		hasCubeMap = false;
		GLint uniBaseMap = prog->uniformLocations[SAMP_BASE];
		if ( uniBaseMap >= 0 ) {
			if ( !activateTextureUnit( texunit ) || !texprop->bind( 0 ) )
				prog->uniSamplerBlank( SAMP_BASE, texunit );
			else
				fn->glUniform1i( uniBaseMap, texunit++ );
		}
	}

	if ( bsprop && !esp ) {
		const QString *	forced = &emptyString;
		if ( !scene->hasOption(Scene::DoLighting) )
			forced = &default_n;
		prog->uniSampler( bsprop, SAMP_NORMAL, 1, texunit, emptyString, clamp, *forced );
	} else if ( !bsprop ) {
		GLint uniNormalMap = prog->uniformLocations[SAMP_NORMAL];
		if ( uniNormalMap >= 0 && activateTextureUnit( texunit ) ) {
			QString fname = texprop->fileName( 0 );
			if ( !fname.isEmpty() ) {
				int pos = fname.lastIndexOf( "_" );
				if ( pos >= 0 )
					fname = fname.left( pos ) + "_n.dds";
				else if ( (pos = fname.lastIndexOf( "." )) >= 0 )
					fname = fname.insert( pos, "_n" );
			}

			if ( fname.isEmpty() || !texprop->bind( 0, fname ) ) {
				texprop->bind( 0, default_n );
			} else {
				auto	t = scene->getTextureInfo( fname );
				if ( t && ( t->format.imageEncoding & TexCache::TexFmt::TEXFMT_DXT1 ) != 0 ) {
					// disable specular for Oblivion normal maps in BC1 format
					hasSpecular = false;
				}
			}
			fn->glUniform1i( uniNormalMap, texunit++ );
		}
	}

	if ( bsprop && !esp ) {
		hasGlowMap = !bsprop->fileName( 2 ).isEmpty();
		prog->uniSampler( bsprop, SAMP_GLOW, 2, texunit, black, clamp );

		// Parallax
		prog->uniSampler( bsprop, SAMP_HEIGHT, 3, texunit, gray, clamp );

		// Environment Mapping (always bind cube and mask regardless of shader settings)

		GLint uniCubeMap = prog->uniformLocations[SAMP_CUBE];
		if ( uniCubeMap < 0 ) {
			hasCubeMap = false;
		} else {
			if ( !activateTextureUnit( texunit ) )
				return false;
			QString	fname = bsprop->fileName( 4 );
			if ( hasCubeMap && !fname.isEmpty() )
				hasCubeMap = bsprop->bindCube( fname );
			if ( !hasCubeMap )
				bsprop->bindCube( "#FF555555c" );
			fn->glUniform1i( uniCubeMap, texunit++ );
		}

		hasCubeMask = !bsprop->fileName( 5 ).isEmpty();
		prog->uniSampler( bsprop, SAMP_ENV_MASK, 5, texunit, white, clamp );

	} else if ( !bsprop ) {
		GLint uniGlowMap = prog->uniformLocations[SAMP_GLOW];
		if ( uniGlowMap >= 0 && activateTextureUnit( texunit ) ) {
			bool	result = false;
			QString fname = texprop->fileName( 0 );
			if ( !fname.isEmpty() ) {
				int pos = fname.lastIndexOf( "_" );
				if ( pos >= 0 )
					fname = fname.left( pos ) + "_g.dds";
				else if ( (pos = fname.lastIndexOf( "." )) >= 0 )
					fname = fname.insert( pos, "_g" );
			}

			if ( !fname.isEmpty() && texprop->bind( 0, fname ) )
				result = true;

			hasGlowMap = result;
			if ( !result )
				texprop->bind( 0, black );
			fn->glUniform1i( uniGlowMap, texunit++ );
		}
	}

	if ( texprop ) {
		auto	t = texprop->getTexture( 0 );
		if ( t && t->hasTransform ) {
			uvScaleAndOffset = FloatVector4( t->tiling[0], t->tiling[1], t->translation[0], t->translation[1] );
			uvCenterAndRotation = FloatVector4( t->center[0], t->center[1], t->rotation, 0.0f );
		}
		const NifItem *	i = texprop->getItem( nif, "Apply Mode" );
		if ( i ) {
			quint32	applyMode = nif->get<quint32>( i );
			isDecal = ( applyMode == 1 );
			if ( applyMode == 4 )
				parallaxMaxSteps = 1;
		}
	}
	if ( bsprop ) {
		isDecal = bsprop->hasSF1( ShaderFlags::SLSF1_Decal ) | bsprop->hasSF1( ShaderFlags::SLSF1_Dynamic_Decal );
		hasSpecular = hasSpecular && bsprop->hasSF1( ShaderFlags::SLSF1_Specular );
		hasCubeMap = hasCubeMap && bsprop->hasSF1( ShaderFlags::SLSF1_Environment_Mapping );
		cubeMapScale = bsprop->environmentReflection;
		if ( bsprop->hasSF1( ShaderFlags::SLSF1_Parallax_Occlusion ) ) {
			const NifItem *	i = bsprop->getItem( nif, "Parallax Max Passes" );
			if ( i )
				parallaxMaxSteps = std::max< int >( roundFloat( nif->get<float>(i) ), 4 );
			i = bsprop->getItem( nif, "Parallax Scale" );
			if ( i )
				parallaxScale *= nif->get<float>( i );
		} else if ( bsprop->hasSF1( ShaderFlags::SLSF1_Parallax ) ) {
			parallaxMaxSteps = 1;
		}
		if ( esp ) {
			glowMult = 1.0f;
			falloffParams = FloatVector4( esp->falloff.startAngle, esp->falloff.stopAngle,
											esp->falloff.startOpacity, esp->falloff.stopOpacity );
		}
	} else {
		hasEmit = true;
	}

	for ( auto p = mesh->findProperty< VertexColorProperty >(); p; ) {
		vertexColorFlags = ( ( p->lightmode & 1 ) << 3 ) | ( ( p->vertexmode & 3 ) << 4 );
		break;
	}
	prog->uni1i_l( prog->uniLocation("vertexColorFlags"), vertexColorFlags );
	prog->uni1b_l( prog->uniLocation("isEffect"), bool(esp) );
	prog->uni1b_l( prog->uniLocation("hasSpecular"), hasSpecular );
	prog->uni1b_l( prog->uniLocation("hasEmit"), hasEmit );
	prog->uni1b_l( prog->uniLocation("hasGlowMap"), hasGlowMap );
	prog->uni1b_l( prog->uniLocation("hasCubeMap"), hasCubeMap );
	prog->uni1b_l( prog->uniLocation("hasCubeMask"), hasCubeMask );
	prog->uni1f_l( prog->uniLocation("cubeMapScale"), cubeMapScale );
	prog->uni1i_l( prog->uniLocation("parallaxMaxSteps"), parallaxMaxSteps );
	prog->uni1f_l( prog->uniLocation("parallaxScale"), parallaxScale );
	prog->uni1f_l( prog->uniLocation("glowMult"), glowMult );
	prog->uni4f_l( prog->uniLocation("glowColor"), glowColor );
	prog->uni2f_l( prog->uniLocation("uvCenter"), uvCenterAndRotation[0], uvCenterAndRotation[1] );
	prog->uni2f_l( prog->uniLocation("uvScale"), uvScaleAndOffset[0], uvScaleAndOffset[1] );
	prog->uni2f_l( prog->uniLocation("uvOffset"), uvScaleAndOffset[2], uvScaleAndOffset[3] );
	prog->uni1f_l( prog->uniLocation("uvRotation"), uvCenterAndRotation[2] );
	prog->uni4f_l( prog->uniLocation("falloffParams"), falloffParams );

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

	if ( mesh->isDoubleSided ) {
		glDisable( GL_CULL_FACE );
	} else {
		glEnable( GL_CULL_FACE );
		glCullFace( GL_BACK );
	}

	if ( !mesh->depthTest ) {
		glDisable( GL_DEPTH_TEST );
	} else {
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LEQUAL );
	}
	glDepthMask( !mesh->depthWrite || mesh->translucent ? GL_FALSE : GL_TRUE );

	// setup blending

	glProperty( mesh->alphaProperty );

	if ( isDecal ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( -1.0f, -1.0f );
	}

	// setup material

	glProperty( mesh->findProperty< MaterialProperty >(), mesh->findProperty< SpecularProperty >() );

	// setup Z buffer

	for ( auto p = mesh->findProperty< ZBufferProperty >(); p; ) {
		glProperty( p );
		break;
	}

	// setup stencil

	glProperty( mesh->findProperty< StencilProperty >() );

	glDisable( GL_COLOR_MATERIAL );
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

	return true;
}

void Renderer::setupFixedFunction( Shape * mesh )
{
	PropertyList props;
	mesh->activeProperties( props );

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

void Renderer::drawSkyBox( Scene * scene )
{
	static const std::uint16_t	skyBoxTriangles[36] = {
		1, 5, 3,  3, 5, 7,  0, 2, 4,  4, 2, 6,	// +X, -X
		2, 3, 6,  6, 3, 7,  0, 4, 1,  1, 4, 5,	// +Y, -Y
		4, 6, 5,  5, 6, 7,  0, 1, 2,  2, 1, 3	// +Z, -Z
	};
	static const float	skyBoxVertices[24] = {
		-10.0f, -10.0f, -10.0f,   10.0f, -10.0f, -10.0f,  -10.0f,  10.0f, -10.0f,   10.0f,  10.0f, -10.0f,
		-10.0f, -10.0f,  10.0f,   10.0f, -10.0f,  10.0f,  -10.0f,  10.0f,  10.0f,   10.0f,  10.0f,  10.0f
	};

	if ( cfg.cubeBgndMipLevel < 0 || !scene->nifModel || scene->nifModel->getBSVersion() < 151 || Node::SELECTING
		|| scene->hasVisMode( Scene::VisSilhouette ) ) {
		return;
	}

	const NifModel *	nif = scene->nifModel;
	quint32	bsVersion = nif->getBSVersion();
	static const QString	programName = "skybox.prog";
	Program *	prog = programs.value( programName );
	if ( !prog || !fn )
		return;

	Transform	vt = scene->view;
	vt.translation = Vector3( 0.0, 0.0, 0.0 );

	glLoadIdentity();
	glPushMatrix();
	glMultMatrix( vt );
	glDisable( GL_POLYGON_OFFSET_FILL );
	glEnableClientState( GL_VERTEX_ARRAY );
	glVertexPointer( 3, GL_FLOAT, 0, skyBoxVertices );
	glEnable( GL_FRAMEBUFFER_SRGB );

	fn->glUseProgram( prog->id );

	// texturing

	int	texunit = 0;

	// Always bind cube to texture unit 0, regardless of shader settings
	bool	hasCubeMap = scene->hasOption(Scene::DoCubeMapping) && scene->hasOption(Scene::DoLighting);
	GLint	uniCubeMap = prog->uniformLocations[SAMP_CUBE];
	if ( uniCubeMap < 0 || !activateTextureUnit( texunit ) ) {
		stopProgram();
		glDisableClientState( GL_VERTEX_ARRAY );
		glPopMatrix();
		return;
	}
	if ( hasCubeMap )
		hasCubeMap = scene->bindTexture( bsVersion < 170 ? cfg.cubeMapPathFO76 : cfg.cubeMapPathSTF );
	if ( !hasCubeMap )
		scene->bindTexture( "#FF555555c", false, true );
	fn->glUniform1i( uniCubeMap, texunit++ );

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

	prog->uni1i( HAS_MAP_CUBE, hasCubeMap );
	prog->uni1b_l( prog->uniLocation("invertZAxis"), ( bsVersion < 170 ) );
	prog->uni1i_l( prog->uniLocation("skyCubeMipLevel"), cfg.cubeBgndMipLevel );

	prog->uni4m( MAT_VIEW, vt.toMatrix4() );

	glDisable( GL_BLEND );
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_COLOR_MATERIAL );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );
	glDepthFunc( GL_LEQUAL );
	glEnable( GL_CULL_FACE );
	glCullFace( GL_BACK );
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

	glEnableClientState( GL_NORMAL_ARRAY );
	glNormalPointer( GL_FLOAT, 0, skyBoxVertices );

	glDrawElements( GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, skyBoxTriangles );

	stopProgram();
	glDisableClientState( GL_VERTEX_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );
	glPopMatrix();
}
