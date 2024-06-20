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

#ifndef GLSHADER_H
#define GLSHADER_H

#include <data/niftypes.h>

#include <QCoreApplication>
#include <QMap>
#include <QVector>
#include <QString>

#include <array>
#include <string>

#include "material.hpp"

//! @file renderer.h Renderer, Renderer::ConditionSingle, Renderer::ConditionGroup, Renderer::Shader, Renderer::Program

class NifModel;
class Shape;
class PropertyList;

class QOpenGLContext;
class QOpenGLFunctions;

typedef unsigned int GLenum;
typedef unsigned int GLuint;

//! Manages rendering and shaders
class Renderer : public QObject
{
	Q_OBJECT

	friend class Program;

public:
	Renderer( QOpenGLContext * c, QOpenGLFunctions * f );
	~Renderer();

	//! Set up shaders
	bool initialize();
	//! Whether shader support is available
	bool hasShaderSupport();

	//! Updates shaders
	void updateShaders();
	//! Releases shaders
	void releaseShaders();

	//! Context
	QOpenGLContext * cx;
	//! Context Functions
	QOpenGLFunctions * fn;

	//! Set up shader program
	QString setupProgram( Shape *, const QString & hint = {} );
	//! Stop shader program
	void stopProgram();

	typedef enum
	{
		// Samplers
		SAMP_BASE = 0,
		SAMP_NORMAL,
		SAMP_SPECULAR,
		SAMP_REFLECTIVITY,
		SAMP_LIGHTING,
		SAMP_CUBE,
		SAMP_CUBE_2,
		SAMP_ENV_MASK,
		SAMP_GLOW,
		SAMP_HEIGHT,
		SAMP_GRAYSCALE,
		SAMP_DETAIL,
		SAMP_TINT,
		SAMP_LIGHT,
		SAMP_BACKLIGHT,
		SAMP_INNER,
		// Uniforms
		ALPHA,
		DOUBLE_SIDE,
		ENV_REFLECTION,
		FALL_DEPTH,
		FALL_PARAMS,
		G2P_ALPHA,
		G2P_COLOR,
		G2P_SCALE,
		GLOW_COLOR,
		GLOW_MULT,
		HAS_EMIT,
		HAS_MAP_BACK,
		HAS_MAP_BASE,
		HAS_MAP_CUBE,
		HAS_MAP_DETAIL,
		HAS_MAP_G2P,
		HAS_MAP_GLOW,
		HAS_MAP_HEIGHT,
		HAS_MAP_NORMAL,
		HAS_MAP_SPEC,
		HAS_MAP_TINT,
		HAS_MASK_ENV,
		HAS_RGBFALL,
		HAS_RIM,
		HAS_SOFT,
		HAS_TINT_COLOR,
		HAS_WEAP_BLOOD,
		INNER_SCALE,
		INNER_THICK,
		LIGHT_EFF1,
		LIGHT_EFF2,
		LIGHT_INF,
		MAT_VIEW,
		MAT_WORLD,
		OUTER_REFL,
		OUTER_REFR,
		POW_BACK,
		POW_FRESNEL,
		POW_RIM,
		HAS_SPECULAR,
		SPEC_COLOR,
		SPEC_GLOSS,
		SPEC_SCALE,
		SS_ROLLOFF,
		TINT_COLOR,
		USE_FALLOFF,
		UV_OFFSET,
		UV_SCALE,
		SKINNED,
		GPU_SKINNED,
		GPU_BONES,
		WIREFRAME,
		SOLID_COLOR,
		LUM_EMIT,

		NUM_UNIFORM_TYPES
	} UniformType;

public slots:
	void updateSettings();

protected:
	//! Base Condition class for shader programs
	class Condition
	{
public:
		Condition() {}
		virtual ~Condition() {}

		virtual bool eval( const NifModel * nif, const QVector<QModelIndex> & iBlocks ) const = 0;
	};

	//! Condition class for single conditions
	class ConditionSingle final : public Condition
	{
public:
		ConditionSingle( const QString & line, bool neg = false );

		bool eval( const NifModel * nif, const QVector<QModelIndex> & iBlocks ) const override final;

protected:
		QString left, right;
		enum Type
		{
			NONE, EQ, NE, LE, GE, LT, GT, AND, NAND
		};
		Type comp;
		const static QHash<Type, QString> compStrs;

		bool invert;

		QModelIndex getIndex( const NifModel * nif, const QVector<QModelIndex> & iBlock, QString name ) const;
		template <typename T> bool compare( T a, T b ) const;
	};

	//! Condition class for grouped conditions (OR or AND)
	class ConditionGroup final : public Condition
	{
public:
		ConditionGroup( bool o = false ) { _or = o; }
		~ConditionGroup() { qDeleteAll( conditions ); }

		bool eval( const NifModel * nif, const QVector<QModelIndex> & iBlocks ) const override final;

		void addCondition( Condition * c );

		bool isOrGroup() const { return _or; }

protected:
		QVector<Condition *> conditions;
		bool _or;
	};

	//! Parsing and loading of .frag or .vert files
	class Shader
	{
public:
		Shader( const QString & name, GLenum type, QOpenGLFunctions * fn );
		~Shader();

		bool load( const QString & filepath );

		QOpenGLFunctions * f;
		QString name;
		GLuint id;
		bool status;

protected:
		GLenum type;
	};

	//! Parsing and loading of .prog files
	class Program
	{
public:
		Program( const QString & name, QOpenGLFunctions * fn );
		~Program();

		bool load( const QString & filepath, Renderer * );

		typedef enum
		{
			CT_BASE = 0,
			CT_DARK,
			CT_DETAIL,
			CT_GLOSS,
			CT_GLOW,
			CT_BUMP,
			CT_DECAL0,
			CT_DECAL1,
			CT_DECAL2,
			CT_DECAL3,
			CT_TANGENT = 24,
			CT_BITANGENT,
			CT_BONE,
			CT_WEIGHT

		} CoordType;

		QOpenGLFunctions * f;
		QString name;
		GLuint id;
		bool status = false;

		ConditionGroup conditions;
		QMap<int, CoordType> texcoords;

		std::array<std::string, NUM_UNIFORM_TYPES> uniforms = { {
			"BaseMap",
			"NormalMap",
			"SpecularMap",
			"ReflMap",
			"LightingMap",
			"CubeMap",
			"CubeMap2",
			"EnvironmentMap",
			"GlowMap",
			"HeightMap",
			"GreyscaleMap",
			"DetailMask",
			"TintMask",
			"LightMask",
			"BacklightMap",
			"InnerMap",
			"alpha",
			"doubleSided",
			"envReflection",
			"falloffDepth",
			"falloffParams",
			"greyscaleAlpha",
			"greyscaleColor",
			"paletteScale",
			"glowColor",
			"glowMult",
			"hasEmit",
			"hasBacklight",
			"hasSourceTexture",
			"hasCubeMap",
			"hasDetailMask",
			"hasGreyscaleMap",
			"hasGlowMap",
			"hasHeightMap",
			"hasNormalMap",
			"hasSpecularMap",
			"hasTintMask",
			"hasEnvMask",
			"hasRGBFalloff",
			"hasRimlight",
			"hasSoftlight",
			"hasTintColor",
			"hasWeaponBlood",
			"innerScale",
			"innerThickness",
			"lightingEffect1",
			"lightingEffect2",
			"lightingInfluence",
			"viewMatrix",
			"worldMatrix",
			"outerReflection",
			"outerRefraction",
			"backlightPower",
			"fresnelPower",
			"rimPower",
			"hasSpecular",
			"specColor",
			"specGlossiness",
			"specStrength",
			"subsurfaceRolloff",
			"tintColor",
			"useFalloff",
			"uvOffset",
			"uvScale",
			"isSkinned",
			"isGPUSkinned",
			"boneTransforms",
			"isWireframe",
			"solidColor",
			"fLumEmittance"
		} };

		int uniformLocations[NUM_UNIFORM_TYPES];
private:
		struct UniformLocationMapItem {
			const char *	fmt;
			std::uint32_t	args;
			int	l;
			inline UniformLocationMapItem()
				: fmt( nullptr ), args( 0 ), l( -1 )
			{
			}
			inline UniformLocationMapItem( const char *s, int argsX16Y16 );
			inline bool operator==( const UniformLocationMapItem & r ) const;
			inline std::uint32_t hashFunction() const;
		};
		UniformLocationMapItem *	uniLocationsMap;
		unsigned int	uniLocationsMapMask;
		unsigned int	uniLocationsMapSize;
		int storeUniformLocation( const UniformLocationMapItem & o, size_t i );
public:
		void setUniformLocations();

		void uni1f( UniformType var, float x );
		void uni2f( UniformType var, float x, float y );
		void uni3f( UniformType var, float x, float y, float z );
		void uni4f( UniformType var, float x, float y, float z, float w );
		void uni1i( UniformType var, int val );
		void uni3m( UniformType var, const Matrix & val );
		void uni4m( UniformType var, const Matrix4 & val );
		bool uniSampler( class BSShaderLightingProperty * bsprop, UniformType var, int textureSlot,
							int & texunit, const QString & alternate, uint clamp, const QString & forced = {} );
		bool uniSamplerBlank( UniformType var, int & texunit );

		// fmt must be a string literal, with at most two %d format integer arguments in the range 0 to 99
		int uniLocation( const char * fmt, int argsX16Y16 = 0 );
		inline int uniLocation( const char * fmt, int arg1, int arg2 )
		{
			return uniLocation( fmt, arg1 | ( arg2 << 16 ) );
		}
		void uni1b_l( int l, bool x );
		void uni1i_l( int l, int x );
		void uni1f_l( int l, float x );
		void uni2f_l( int l, float x, float y );
		void uni4f_l( int l, FloatVector4 x );
		void uni4srgb_l( int l, FloatVector4 x );
		void uni4c_l( int l, std::uint32_t c, bool isSRGB = false );
		void uni1bv_l( int l, const bool * x, size_t n );
		void uni1iv_l( int l, const int * x, size_t n );
		void uni1fv_l( int l, const float * x, size_t n );
		void uni4fv_l( int l, const FloatVector4 * x, size_t n );
		void uniSampler_l( int l, int firstTextureUnit, int textureCnt, int arraySize );
	};

	QMap<QString, Shader *> shaders;
	QMap<QString, Program *> programs;

	bool setupProgramSF( Program *, Shape * );
	bool setupProgram( Program *, Shape *, const PropertyList &, const QVector<QModelIndex> & iBlocks, bool eval = true );
	void setupFixedFunction( Shape *, const PropertyList & );

	struct Settings
	{
		bool	useShaders = true;
		short	sfParallaxMaxSteps = 200;
		float	sfParallaxScale = 0.0f;
		float	sfParallaxOffset = 0.5f;
		QString	cubeMapPathFO76;
		QString	cubeMapPathSTF;
	} cfg;
};


// Templates

template <typename T> inline bool Renderer::ConditionSingle::compare( T a, T b ) const
{
	switch ( comp ) {
	case EQ:
		return a == b;
	case NE:
		return a != b;
	case LE:
		return a <= b;
	case GE:
		return a >= b;
	case LT:
		return a < b;
	case GT:
		return a > b;
	case AND:
		return a & b;
	case NAND:
		return !(a & b);
	default:
		return true;
	}
}

template <> inline bool Renderer::ConditionSingle::compare( float a, float b ) const
{
	switch ( comp ) {
	case EQ:
		return a == b;
	case NE:
		return a != b;
	case LE:
		return a <= b;
	case GE:
		return a >= b;
	case LT:
		return a < b;
	case GT:
		return a > b;
	default:
		return true;
	}
}

template <> inline bool Renderer::ConditionSingle::compare( QString a, QString b ) const
{
	switch ( comp ) {
	case EQ:
		return a == b;
	case NE:
		return a != b;
	default:
		return false;
	}
}

#endif
