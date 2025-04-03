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

#include "glproperty.h"

#include "message.h"
#include "gl/controllers.h"
#include "gl/glscene.h"
#include "gl/gltex.h"
#include "io/material.h"
#include "gamemanager.h"
#include "libfo76utils/src/ddstxt16.hpp"
#include "glview.h"
#include "renderer.h"


//! @file glproperty.cpp Encapsulation of NiProperty blocks defined in nif.xml

static const std::pair< const std::string_view, int >	niPropertyBlockNames[17] = {
	{ "BSEffectShaderProperty", 11 },
	{ "BSLightingShaderProperty", 9 },
	{ "BSShaderLightingProperty", 10 },
	{ "BSShaderNoLightingProperty", 11 },
	{ "BSShaderPPLightingProperty", 10 },
	{ "BSWaterShaderProperty", 12 },
	{ "Lighting30ShaderProperty", 10 },
	{ "NiAlphaProperty", 0 },
	{ "NiMaterialProperty", 4 },
	{ "NiSpecularProperty", 5 },
	{ "NiStencilProperty", 8 },
	{ "NiTextureProperty", 3 },
	{ "NiTexturingProperty", 2 },
	{ "NiVertexColorProperty", 7 },
	{ "NiWireframeProperty", 6 },
	{ "NiZBufferProperty", 1 },
	{ "TallGrassShaderProperty", 10 }
};

Property * Property::create( Scene * scene, const NifModel * nif, const QModelIndex & index )
{
	Property * property = nullptr;

	if ( auto blockItem = nif->getItem( index ); blockItem && nif->isNiBlock( blockItem ) ) {
		constexpr size_t	n = sizeof( niPropertyBlockNames ) / sizeof( niPropertyBlockNames[0] );
		const QString &	blockName = blockItem->name();
		size_t	i0 = 0;
		size_t	i2 = n;
		int	i = -1;
		while ( i2 > i0 ) {
			size_t	i1 = ( i0 + i2 ) >> 1;
			const auto &	p = niPropertyBlockNames[i1];
			int	d = blockName.compare( QLatin1String( p.first.data(), qsizetype( p.first.length() ) ) );
			if ( !d ) {
				i = p.second;
				break;
			}
			if ( d < 0 )
				i2 = i1;
			else
				i0 = i1 + 1;
		}
		switch ( i ) {
		case 0:   property = new AlphaProperty( scene, index );             break;
		case 1:   property = new ZBufferProperty( scene, index );           break;
		case 2:   property = new TexturingProperty( scene, index );         break;
		case 3:   property = new TextureProperty( scene, index );           break;
		case 4:   property = new MaterialProperty( scene, index );          break;
		case 5:   property = new SpecularProperty( scene, index );          break;
		case 6:   property = new WireframeProperty( scene, index );         break;
		case 7:   property = new VertexColorProperty( scene, index );       break;
		case 8:   property = new StencilProperty( scene, index );           break;
		case 9:   property = new BSLightingShaderProperty( scene, index );  break;
		case 10:  property = new BSShaderLightingProperty( scene, index );  break;
		case 11:  property = new BSEffectShaderProperty( scene, index );    break;
		case 12:  property = new BSWaterShaderProperty( scene, index );     break;
		}
	} else if ( index.isValid() ) {
#ifndef QT_NO_DEBUG
		NifItem * item = static_cast<NifItem *>( index.internalPointer() );

		if ( item )
			qCWarning( nsNif ) << tr( "Unknown property: %1" ).arg( item->name() );
		else
			qCWarning( nsNif ) << tr( "Unknown property: I can't determine its name" );
#endif
	}

	if ( property )
		property->update( nif, index );

	return property;
}

PropertyList::PropertyList()
{
}

PropertyList::PropertyList( const PropertyList & other )
{
	operator=( other );
}

PropertyList::~PropertyList()
{
	clear();
}

void PropertyList::clear()
{
	for ( Property * p : properties ) {
		if ( --p->ref <= 0 )
			delete p;
	}
	properties.clear();
}

PropertyList & PropertyList::operator=( const PropertyList & other )
{
	clear();
	for ( Property * p : other.properties ) {
		add( p );
	}
	return *this;
}

bool PropertyList::contains( Property * p ) const
{
	if ( !p )
		return false;

	QList<Property *> props = properties.values( p->type() );
	return props.contains( p );
}

void PropertyList::add( Property * p )
{
	if ( p && !contains( p ) ) {
		++p->ref;
		properties.insert( p->type(), p );
	}
}

void PropertyList::del( Property * p )
{
	if ( !p )
		return;

	QMultiHash<Property::Type, Property *>::iterator i = properties.find( p->type() );

	while ( p && i != properties.end() && i.key() == p->type() ) {
		if ( i.value() == p ) {
			i = properties.erase( i );

			if ( --p->ref <= 0 )
				delete p;
		} else {
			++i;
		}
	}
}

Property * PropertyList::get( const QModelIndex & index ) const
{
	if ( !index.isValid() )
		return 0;

	for ( Property * p : properties ) {
		if ( p->index() == index )
			return p;
	}
	return 0;
}

void PropertyList::validate()
{
	QList<Property *> rem;
	for ( Property * p : properties ) {
		if ( !p->isValid() )
			rem.append( p );
	}
	for ( Property * p : rem ) {
		del( p );
	}
}

void PropertyList::merge( const PropertyList & other )
{
	for ( Property * p : other.properties ) {
		if ( !properties.contains( p->type() ) )
			add( p );
	}
}

void AlphaProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		unsigned short flags = nif->get<int>( iBlock, "Flags" );

		alphaBlend = flags & 1;

		static const GLenum blendMap[16] = {
			GL_ONE, GL_ZERO, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
			GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA_SATURATE, GL_ONE,
			GL_ONE, GL_ONE, GL_ONE, GL_ONE
		};

		alphaSrc = blendMap[ ( flags >> 1 ) & 0x0f ];
		alphaDst = blendMap[ ( flags >> 5 ) & 0x0f ];

		alphaThreshold = float( nif->get<int>( iBlock, "Threshold" ) ) / 255.0;

		alphaSort = ( flags & 0x2000 ) == 0;

		alphaTest = flags & ( 1 << 9 );
		// Temporary Weapon Blood fix for FO4
		if ( nif->getBSVersion() >= 130 )
			alphaTest |= (flags == 20547);

		int i = ( flags >> 10 ) & 0x7;
		alphaFunc = std::int8_t( alphaTest ? i : -1 );
	}
}

void AlphaProperty::setController( const NifModel * nif, const QModelIndex & controller )
{
	auto contrName = nif->itemName(controller);
	if ( contrName == "BSNiAlphaPropertyTestRefController" ) {
		Controller * ctrl = new AlphaController( this, controller );
		registerController(nif, ctrl);
	}
}

void AlphaProperty::glProperty( AlphaProperty * p, NifSkopeOpenGLContext::Program * prog )
{
	int	alphaFlags = 0;
	if ( p && p->alphaBlend && p->scene->hasOption(Scene::DoBlending) ) {
		glEnable( GL_BLEND );
		prog->f->glBlendFuncSeparate( p->alphaSrc, p->alphaDst, GL_ONE, p->alphaDst );
		alphaFlags = 8;
	} else {
		glDisable( GL_BLEND );
	}

	if ( !prog )
		return;

	// test function (-1: disabled, 0: always, 1: <, 2: ==, 3: <=, 4: >, 5: !=, 6: >=, 7: never)
	float	alphaTestThreshold = 0.0f;
	if ( p && p->alphaTest && p->scene->hasOption(Scene::DoBlending) ) {
		alphaFlags |= std::max< int >( p->alphaFunc, 0 );
		alphaTestThreshold = p->alphaThreshold;
	}
	prog->uni1i( "alphaFlags", alphaFlags );
	prog->uni1f( "alphaThreshold", alphaTestThreshold );
}

void ZBufferProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		int flags = nif->get<int>( iBlock, "Flags" );
		depthTest = flags & 1;
		depthMask = flags & 2;
		static const GLenum depthMap[8] = {
			GL_ALWAYS, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_NEVER
		};

		// This was checking version 0x10000001 ?
		if ( nif->checkVersion( 0x04010012, 0x14000005 ) ) {
			depthFunc = depthMap[ nif->get < int > ( iBlock, "Function" ) & 0x07 ];
		} else if ( nif->checkVersion( 0x14010003, 0 ) ) {
			depthFunc = depthMap[ (flags >> 2 ) & 0x07 ];
		} else {
			depthFunc = GL_LEQUAL;
		}
	}
}

void ZBufferProperty::glProperty( ZBufferProperty * p )
{
	if ( p ) {
		if ( p->depthTest ) {
			glEnable( GL_DEPTH_TEST );
			glDepthFunc( p->depthFunc );
		} else {
			glDisable( GL_DEPTH_TEST );
		}

		glDepthMask( p->depthMask ? GL_TRUE : GL_FALSE );
	} else {
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LESS );
		glDepthMask( GL_TRUE );
		glDepthFunc( GL_LEQUAL );
	}
}

/*
    TexturingProperty
*/

void TexturingProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		static const char * texnames[numTextures] = {
			"Base Texture", "Dark Texture", "Detail Texture", "Gloss Texture", "Glow Texture", "Bump Map Texture", "Decal 0 Texture", "Decal 1 Texture", "Decal 2 Texture", "Decal 3 Texture"
		};

		for ( int t = 0; t < numTextures; t++ ) {
			QModelIndex iTex = nif->getIndex( iBlock, texnames[t] );

			if ( iTex.isValid() ) {
				textures[t].iSource  = nif->getBlockIndex( nif->getLink( iTex, "Source" ), "NiSourceTexture" );
				textures[t].coordset = nif->get<int>( iTex, "UV Set" );

				int filterMode = 0, clampMode = 0;
				if ( nif->checkVersion( 0, 0x14010002 ) ) {
					filterMode = nif->get<int>( iTex, "Filter Mode" );
					clampMode  = nif->get<int>( iTex, "Clamp Mode" );
				} else if ( nif->checkVersion( 0x14010003, 0 ) ) {
					auto flags = nif->get<ushort>( iTex, "Flags" );
					filterMode = ((flags & 0x0F00) >> 0x08);
					clampMode  = ((flags & 0xF000) >> 0x0C);
					textures[t].coordset = (flags & 0x00FF);
				}

				float af = 1.0;
				float max_af = TexCache::get_max_anisotropy();
				// Let User Settings decide for trilinear
				if ( filterMode == GL_LINEAR_MIPMAP_LINEAR )
					af = max_af;

				// Override with value in NIF for 20.5+
				if ( nif->checkVersion( 0x14050004, 0 ) )
					af = std::min( max_af, (float)nif->get<ushort>( iTex, "Max Anisotropy" ) );

				textures[t].maxAniso = std::max( 1.0f, std::min( af, max_af ) );

				// See OpenGL docs on glTexParameter and GL_TEXTURE_MIN_FILTER option
				// See also http://gregs-blog.com/2008/01/17/opengl-texture-filter-parameters-explained/
				switch ( filterMode ) {
				case 0:
					textures[t].filter = GL_NEAREST;
					break;             // nearest
				case 1:
					textures[t].filter = GL_LINEAR;
					break;             // bilinear
				case 2:
					textures[t].filter = GL_LINEAR_MIPMAP_LINEAR;
					break;             // trilinear
				case 3:
					textures[t].filter = GL_NEAREST_MIPMAP_NEAREST;
					break;             // nearest from nearest
				case 4:
					textures[t].filter = GL_NEAREST_MIPMAP_LINEAR;
					break;             // interpolate from nearest
				case 5:
					textures[t].filter = GL_LINEAR_MIPMAP_NEAREST;
					break;             // bilinear from nearest
				default:
					textures[t].filter = GL_LINEAR;
					break;
				}

				switch ( clampMode ) {
				case 0:
					textures[t].wrapS = GL_CLAMP_TO_EDGE;
					textures[t].wrapT = GL_CLAMP_TO_EDGE;
					break;
				case 1:
					textures[t].wrapS = GL_CLAMP_TO_EDGE;
					textures[t].wrapT = GL_REPEAT;
					break;
				case 2:
					textures[t].wrapS = GL_REPEAT;
					textures[t].wrapT = GL_CLAMP_TO_EDGE;
					break;
				default:
					textures[t].wrapS = GL_REPEAT;
					textures[t].wrapT = GL_REPEAT;
					break;
				}

				textures[t].hasTransform = nif->get<int>( iTex, "Has Texture Transform" );

				if ( textures[t].hasTransform ) {
					textures[t].translation = nif->get<Vector2>( iTex, "Translation" );
					textures[t].tiling = nif->get<Vector2>( iTex, "Scale" );
					textures[t].rotation = nif->get<float>( iTex, "Rotation" );
					textures[t].center = nif->get<Vector2>( iTex, "Center" );
				} else {
					// we don't really need to set these since they won't be applied in bind() unless hasTransform is set
					textures[t].translation = Vector2();
					textures[t].tiling = Vector2( 1.0, 1.0 );
					textures[t].rotation = 0.0;
					textures[t].center = Vector2( 0.5, 0.5 );
				}
			} else {
				textures[t].iSource = QModelIndex();
			}
		}
	}
}

bool TexturingProperty::bind( int id, const QString & fname )
{
	GLuint mipmaps = 0;

	if ( id >= 0 && id <= (numTextures - 1) ) {
		if ( !fname.isEmpty() )
			mipmaps = scene->bindTexture( fname );
		else
			mipmaps = scene->bindTexture( textures[ id ].iSource );

		if ( mipmaps == 0 )
			return false;

		glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, textures[id].maxAniso );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps > 1 ? textures[id].filter : GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, textures[id].wrapS );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, textures[id].wrapT );

		return true;
	}

	return false;
}

bool TexturingProperty::bind( int id, int stage, NifSkopeOpenGLContext::Program * prog )
{
	int	n = 0;
	if ( id >= 0 && id < numTextures && prog ) {
		const auto &	t = textures[id];
		if ( t.coordset >= 0 && t.coordset < 9 && scene->textures->activateTextureUnit( stage ) && bind( id ) ) {
			n = stage + 1;
			prog->uni1i_l( prog->uniLocation( "textureUnits[%d]", stage ), stage );
			prog->uni2f_l( prog->uniLocation( "textures[%d].uvCenter", id ), t.center[0], t.center[1] );
			prog->uni2f_l( prog->uniLocation( "textures[%d].uvScale", id ), t.tiling[0], t.tiling[1] );
			prog->uni2f_l( prog->uniLocation( "textures[%d].uvOffset", id ), t.translation[0], t.translation[1] );
			prog->uni1f_l( prog->uniLocation( "textures[%d].uvRotation", id ), t.rotation );
			prog->uni1i_l( prog->uniLocation( "textures[%d].coordSet", id ), t.coordset );
			prog->uni1i_l( prog->uniLocation( "textures[%d].textureUnit", id ), n );
		}
	}
	return bool( n );
}

QString TexturingProperty::fileName( int id ) const
{
	if ( id >= 0 && id <= (numTextures - 1) ) {
		QModelIndex iSource = textures[id].iSource;
		auto nif = NifModel::fromValidIndex(iSource);
		if ( nif ) {
			return nif->get<QString>( iSource, "File Name" );
		}
	}

	return QString();
}

int TexturingProperty::coordSet( int id ) const
{
	if ( id >= 0 && id <= (numTextures - 1) ) {
		return textures[id].coordset;
	}

	return -1;
}


//! Set the appropriate Controller
void TexturingProperty::setController( const NifModel * nif, const QModelIndex & iController )
{
	auto contrName = nif->itemName(iController);
	if ( contrName == "NiFlipController" ) {
		Controller * ctrl = new TexFlipController( this, iController );
		registerController(nif, ctrl);
	} else if ( contrName == "NiTextureTransformController" ) {
		Controller * ctrl = new TexTransController( this, iController );
		registerController(nif, ctrl);
	}
}

int TexturingProperty::getId( const QString & texname )
{
	const static QHash<QString, int> hash{
		{ "base",   0 },
		{ "dark",   1 },
		{ "detail", 2 },
		{ "gloss",  3 },
		{ "glow",   4 },
		{ "bumpmap", 5 },
		{ "decal0", 6 },
		{ "decal1", 7 },
		{ "decal2", 8 },
		{ "decal3", 9 }
	};

	return hash.value( texname, -1 );
}

/*
    TextureProperty
*/

void TextureProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		iImage = nif->getBlockIndex( nif->getLink( iBlock, "Image" ), "NiImage" );
	}
}

bool TextureProperty::bind( NifSkopeOpenGLContext::Program * prog )
{
	GLuint	mipmaps;
	if ( !( prog && ( mipmaps = scene->bindTexture( fileName() ) ) != 0 ) )
		return false;

	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR );

	prog->uni1i_l( prog->uniLocation( "textureUnits[%d]", 0 ), 0 );
	prog->uni2f_l( prog->uniLocation( "textures[%d].uvCenter", 0 ), 0.5f, 0.5f );
	prog->uni2f_l( prog->uniLocation( "textures[%d].uvScale", 0 ), 1.0f, 1.0f );
	prog->uni2f_l( prog->uniLocation( "textures[%d].uvOffset", 0 ), 0.0f, 0.0f );
	prog->uni1f_l( prog->uniLocation( "textures[%d].uvRotation", 0 ), 0.0f );
	prog->uni1i_l( prog->uniLocation( "textures[%d].coordSet", 0 ), 0 );
	prog->uni1i_l( prog->uniLocation( "textures[%d].textureUnit", 0 ), 1 );

	return true;
}

QString TextureProperty::fileName() const
{
	auto nif = NifModel::fromValidIndex(iImage);
	if ( nif )
		return nif->get<QString>( iImage, "File Name" );

	return QString();
}


void TextureProperty::setController( const NifModel * nif, const QModelIndex & iController )
{
	auto contrName = nif->itemName(iController);
	if ( contrName == "NiFlipController" ) {
		Controller * ctrl = new TexFlipController( this, iController );
		registerController(nif, ctrl);
	}
}

/*
    MaterialProperty
*/

void MaterialProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		alpha = std::min( std::max( nif->get<float>( iBlock, "Alpha" ), 0.0f ), 1.0f );

		const NifItem *	i = nif->getItem( iBlock, "Ambient Color" );
		if ( !i )
			ambient = Color4();
		else
			ambient = Color4( nif->get<Color3>( i ) );
		diffuse  = Color4( nif->get<Color3>( iBlock, "Diffuse Color" ) );
		specular = Color4( nif->get<Color3>( iBlock, "Specular Color" ) );
		Color3 tmp = nif->get<Color3>( iBlock, "Emissive Color" );
		if ( nif->getBSVersion() > 21 )
			tmp = tmp * nif->get<float>( iBlock, "Emissive Mult" );
		emissive = Color4( tmp );

		// OpenGL needs shininess clamped otherwise it generates GL_INVALID_VALUE
		shininess = std::min( std::max( nif->get<float>( iBlock, "Glossiness" ), 0.0f ), 128.0f );
	}
}

void MaterialProperty::setController( const NifModel * nif, const QModelIndex & iController )
{
	auto contrName = nif->itemName(iController);
	if ( contrName == "NiAlphaController" ) {
		Controller * ctrl = new AlphaController( this, iController );
		registerController(nif, ctrl);
	} else if ( contrName == "NiMaterialColorController" ) {
		Controller * ctrl = new MaterialColorController( this, iController );
		registerController(nif, ctrl);
	}
}


void MaterialProperty::glProperty( MaterialProperty * p, SpecularProperty * s, NifSkopeOpenGLContext::Program * prog )
{
	if ( !prog )
		return;

	if ( p ) {
		prog->uni1f( "alpha", p->alpha );

		prog->uni4f( "frontMaterialAmbient", FloatVector4( p->ambient ) );
		prog->uni4f( "frontMaterialDiffuse", FloatVector4( p->diffuse ) );
		prog->uni4f( "frontMaterialEmission", FloatVector4( p->emissive ) );

		if ( !s || s->spec ) {
			prog->uni1f( "frontMaterialShininess", p->shininess );
			prog->uni4f( "frontMaterialSpecular", FloatVector4( p->specular ) );
		} else {
			prog->uni1f( "frontMaterialShininess", 0.0f );
			prog->uni4f( "frontMaterialSpecular", FloatVector4( 0.0f, 0.0f, 0.0f, 1.0f ) );
		}
	} else {
		prog->uni1f( "alpha", 1.0f );
		prog->uni1f( "frontMaterialShininess", 33.0f );
		prog->uni4f( "frontMaterialAmbient", FloatVector4( 0.4f, 0.4f, 0.4f, 1.0f ) );
		prog->uni4f( "frontMaterialDiffuse", FloatVector4( 0.8f, 0.8f, 0.8f, 1.0f ) );
		prog->uni4f( "frontMaterialEmission", FloatVector4( 0.0f, 0.0f, 0.0f, 1.0f ) );
		prog->uni4f( "frontMaterialSpecular", FloatVector4( 1.0f, 1.0f, 1.0f, 1.0f ) );
	}
}

void SpecularProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		spec = nif->get<int>( iBlock, "Flags" ) != 0;
	}
}

void WireframeProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		wire = nif->get<int>( iBlock, "Flags" ) != 0;
	}
}

bool WireframeProperty::glProperty( WireframeProperty * p )
{
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	if ( p && p->wire ) {
		NifSkopeOpenGLContext::Program *	prog;
		if ( !( p->scene->renderer && ( prog = p->scene->renderer->useProgram( "wireframe.prog" ) ) != nullptr ) )
			return false;

		prog->uni1f( "lineWidth", GLView::Settings::lineWidthWireframe * 0.625f );
		prog->uni4f( "vertexColorOverride", FloatVector4( 1.0e-15f ).maxValues( p->scene->wireframeColor ) );
		prog->uni1i( "selectionParam", -1 );

		return true;
	}
	return false;
}

void VertexColorProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		if ( nif->checkVersion( 0, 0x14010001 ) ) {
			int vertexmode = nif->get<int>( iBlock, "Vertex Mode" );
			// 0 : source ignore
			// 1 : source emissive
			// 2 : source ambient + diffuse
			int lightmode = nif->get<int>( iBlock, "Lighting Mode" );
			// 0 : emissive
			// 1 : emissive + ambient + diffuse
			vertexColorFlags = ( ( vertexmode & 3 ) << 4 ) | ( ( lightmode & 1 ) << 3 );
		} else {
			vertexColorFlags = nif->get<quint16>( iBlock, "Flags" ) & 0x3F;
		}
	}
}

void VertexColorProperty::glProperty(
	VertexColorProperty * p, FloatVector4 overrideColor, NifSkopeOpenGLContext::Program * prog )
{
	if ( !prog )
		return;

	int	vertexColorFlags = 0x28;
	if ( p )
		vertexColorFlags = p->vertexColorFlags;
	prog->uni4f( "vertexColorOverride", overrideColor );
	prog->uni1i( "vertexColorFlags", vertexColorFlags );
}

void StencilProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	using namespace Stencil;
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		static const GLenum funcMap[8] = {
			GL_NEVER, GL_GEQUAL, GL_NOTEQUAL, GL_GREATER, GL_LEQUAL, GL_EQUAL, GL_LESS, GL_ALWAYS
		};

		static const GLenum opMap[6] = {
			GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT
		};

		int drawMode = 0;
		if ( nif->checkVersion( 0, 0x14000005 ) ) {
			drawMode = nif->get<int>( iBlock, "Draw Mode" );
			func = funcMap[std::min(nif->get<quint32>( iBlock, "Stencil Function" ), (quint32)TEST_MAX - 1 )];
			failop = opMap[std::min( nif->get<quint32>( iBlock, "Fail Action" ), (quint32)ACTION_MAX - 1 )];
			zfailop = opMap[std::min( nif->get<quint32>( iBlock, "Z Fail Action" ), (quint32)ACTION_MAX - 1 )];
			zpassop = opMap[std::min( nif->get<quint32>( iBlock, "Pass Action" ), (quint32)ACTION_MAX - 1 )];
			stencil = (nif->get<quint8>( iBlock, "Stencil Enabled" ) & ENABLE_MASK);
		} else {
			auto flags = nif->get<int>( iBlock, "Flags" );
			drawMode = (flags & DRAW_MASK) >> DRAW_POS;
			func = funcMap[(flags & TEST_MASK) >> TEST_POS];
			failop = opMap[(flags & FAIL_MASK) >> FAIL_POS];
			zfailop = opMap[(flags & ZFAIL_MASK) >> ZFAIL_POS];
			zpassop = opMap[(flags & ZPASS_MASK) >> ZPASS_POS];
			stencil = (flags & ENABLE_MASK);
		}

		switch ( drawMode ) {
		case DRAW_CW:
			cullEnable = true;
			cullMode = GL_FRONT;
			break;
		case DRAW_BOTH:
			cullEnable = false;
			cullMode = GL_BACK;
			break;
		case DRAW_CCW:
		default:
			cullEnable = true;
			cullMode = GL_BACK;
			break;
		}

		ref = nif->get<quint32>( iBlock, "Stencil Ref" );
		mask = nif->get<quint32>( iBlock, "Stencil Mask" );
	}
}

void StencilProperty::glProperty( StencilProperty * p )
{
	if ( p ) {
		if ( p->cullEnable )
			glEnable( GL_CULL_FACE );
		else
			glDisable( GL_CULL_FACE );

		glCullFace( p->cullMode );

		if ( p->stencil ) {
			glEnable( GL_STENCIL_TEST );
			glStencilFunc( p->func, p->ref, p->mask );
			glStencilOp( p->failop, p->zfailop, p->zpassop );
		} else {
			glDisable( GL_STENCIL_TEST );
		}
	} else {
		glEnable( GL_CULL_FACE );
		glCullFace( GL_BACK );
		glDisable( GL_STENCIL_TEST );
	}
}

/*
    BSShaderLightingProperty
*/

BSShaderLightingProperty::~BSShaderLightingProperty()
{
	if ( material )
		delete material;
}

void BSShaderLightingProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		bsVersion = (unsigned short) nif->getBSVersion();
		if ( bsVersion >= 170 ) {
			// Starfield
			setSFMaterial( name );
		} else if ( bsVersion >= 83 ) {
			// Skyrim, Fallout 4, Fallout 76
			if ( bsVersion >= 151 )
				iSPData = nif->getIndex( iBlock, "Shader Property Data" );
			else
				iSPData = iBlock;
			iTextureSet = nif->getBlockIndex( nif->getLink( iSPData, "Texture Set" ), "BSShaderTextureSet" );
		} else {
			// Fallout 3/New Vegas
			iSPData = iBlock;
			iTextureSet = nif->getBlockIndex( nif->getLink( iSPData, "Texture Set" ), "BSShaderTextureSet" );
			flags1 = ShaderFlags::SF1( nif->get<quint32>( iSPData, "Shader Flags" ) );
			flags2 = ShaderFlags::SF2( nif->get<quint32>( iSPData, "Shader Flags 2" ) );
			hasVertexColors = bool( flags2 & ShaderFlags::SLSF2_Vertex_Colors );
			hasVertexAlpha = bool( flags1 & ShaderFlags::SLSF1_Vertex_Alpha );
			isDoubleSided = bool( flags2 & ShaderFlags::SLSF2_Double_Sided );
			if ( nif->isNiBlock( iSPData, "TallGrassShaderProperty" ) )
				isVertexAlphaAnimation = true;
			else
				clampMode = TexClampMode( nif->get<quint32>( iSPData, "Texture Clamp Mode" ) );
			environmentReflection = nif->get<float>( iSPData, "Environment Map Scale" );
			if ( typeid( *this ) == typeid( BSEffectShaderProperty ) ) {
				depthTest = bool( flags1 & ShaderFlags::SLSF1_ZBuffer_Test );
				depthWrite = bool( flags2 & ShaderFlags::SLSF2_ZBuffer_Write );
				BSEffectShaderProperty *	esp = static_cast< BSEffectShaderProperty * >( this );
				esp->falloff.startAngle = nif->get<float>( iSPData, "Falloff Start Angle" );
				esp->falloff.stopAngle = nif->get<float>( iSPData, "Falloff Stop Angle" );
				esp->falloff.startOpacity = nif->get<float>( iSPData, "Falloff Start Opacity" );
				esp->falloff.stopOpacity = nif->get<float>( iSPData, "Falloff Stop Opacity" );
				esp->useFalloff = true;
			}
		}
	}
}

void BSShaderLightingProperty::resetParams()
{
	flags1 = ShaderFlags::SLSF1_ZBuffer_Test;
	flags2 = ShaderFlags::SLSF2_ZBuffer_Write;

	uvScale.reset();
	uvOffset.reset();
	clampMode = WRAP_S_WRAP_T;
	environmentReflection = 0.0f;

	hasVertexColors = false;
	hasVertexAlpha = false;

	depthTest = true;
	depthWrite = true;
	isDoubleSided = false;
	isVertexAlphaAnimation = false;
}

void BSShaderLightingProperty::clear()
{
	Property::clear();

	if ( material ) {
		delete material;
		material = nullptr;
	}

	sf_material = nullptr;
	sfMaterialDB_ID = std::uint64_t(-1);
	sf_material_valid = false;
	materialPath.clear();
	sfMatDataBuf.clear();
}

// replacementMode = 1: linear, 2: sRGB, 3: signed

static inline FloatVector4 convertTextureReplacementColor( std::uint32_t textureReplacement, int replacementMode )
{
	FloatVector4	c( textureReplacement );
	c *= 1.0f / 255.0f;
	if ( replacementMode < 2 )
		return c;
	if ( replacementMode == 2 )
		return DDSTexture16::srgbExpand( c );
	return c + c - 1.0f;
}

int BSShaderLightingProperty::getSFTexture( int & texunit, FloatVector4 & replUniform, const std::string_view & texturePath, std::uint32_t textureReplacement, int textureReplacementMode, const CE2Material::UVStream * uvStream )
{
	do {
		size_t	n = texturePath.length();
		if ( ( n - 1 ) & ~( size_t(1023) ) )
			break;		// empty path or not enough space in tmpBuf
		if ( !( texunit >= 3 && scene->textures->activateTextureUnit( texunit ) ) )
			break;

		TexClampMode	clampMode = TexClampMode::WRAP_S_WRAP_T;
		if ( uvStream ) {
			static const unsigned char	clampModes[4] = {
				TexClampMode::WRAP_S_WRAP_T, TexClampMode::CLAMP_S_CLAMP_T,
				TexClampMode::MIRRORED_S_MIRRORED_T, TexClampMode::BORDER_S_BORDER_T
			};
			clampMode = TexClampMode( clampModes[uvStream->textureAddressMode & 3] );
		}

		// convert std::string_view to a temporary array of QChar
		std::uint16_t	tmpBuf[1024];
		convertStringToUInt16( tmpBuf, texturePath.data(), n );

		if ( !bind( QStringView( tmpBuf, qsizetype( n ) ), false, clampMode ) )
			break;

		if ( clampMode == TexClampMode::BORDER_S_BORDER_T ) [[unlikely]] {
			// use replacement color as border (this may be incorrect)
			FloatVector4	c( convertTextureReplacementColor( textureReplacement, textureReplacementMode ) );
			glTexParameterfv( GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &(c[0]) );
		}

		texunit++;
		return texunit - 3;

	} while ( false );

	if ( textureReplacementMode > 0 ) {
		replUniform = convertTextureReplacementColor( textureReplacement, textureReplacementMode );
		return -1;
	}

	return 0;
}

void BSShaderLightingProperty::setMaterial( const NifModel * nif, const QModelIndex & index, bool isEffect )
{
	if ( material ) {
		delete material;
		material = nullptr;
	}

	bool	nameChanged = false;
	if ( name != materialPath ) {
		materialPath = name;
		nameChanged = true;
	}
	bool	isAbstract = false;
	Material *	newMaterial = nullptr;
	if ( name.endsWith( QLatin1String( !isEffect ? ".bgsm" : ".bgem" ), Qt::CaseInsensitive ) ) {
		if ( bsVersion >= 130 && !nameChanged ) {
			const NifItem *	i = nif->getItem( index, "Material" );
			isAbstract = ( i && nif->get<bool>( i, "Is Modified" ) );
		}
		if ( isEffect )
			newMaterial = new EffectMaterial( name, nif, ( isAbstract ? index : QModelIndex() ) );
		else
			newMaterial = new ShaderMaterial( name, nif, ( isAbstract ? index : QModelIndex() ) );
	}
	if ( bsVersion >= 130 && !isAbstract )
		const_cast< NifModel * >(nif)->loadFO76Material( index, newMaterial );

	if ( newMaterial && !newMaterial->isValid() )
		delete newMaterial;
	else
		material = newMaterial;
}

void BSShaderLightingProperty::setSFMaterial( const QString & mat_name )
{
	sfMaterialDB_ID = std::uint64_t(-1);
	if ( mat_name == materialPath ) {
		const NifModel *	nif = scene->nifModel;
		const NifItem *	i = nif->getItem( iBlock, "Material" );
		if ( i && nif->get<bool>( i, "Is Modified" ) ) {
			sf_material = nullptr;
			sf_material_valid = false;
			sfMatDataBuf.clear();
			sf_material = reinterpret_cast< const CE2Material * >( nif->updateSFMaterial( sfMatDataBuf, iBlock ) );
			if ( sf_material ) [[likely]] {
				sfMaterialDB_ID = std::uint64_t(-2);
				sf_material_valid = true;
			}
			return;
		}
	} else {
		materialPath = mat_name;
	}
	loadSFMaterial();
}

void BSShaderLightingProperty::loadSFMaterial()
{
	if ( sfMaterialDB_ID == std::uint64_t(-2) )		// edited material
		return;
	sf_material = nullptr;
	sfMaterialDB_ID = std::uint64_t(-1);
	sf_material_valid = false;
	sfMatDataBuf.clear();
	const NifModel *	nif = scene->nifModel;
	const CE2Material *	mat = nullptr;
	if ( !materialPath.isEmpty() ) {
		std::string	fullPath( Game::GameManager::get_full_path( materialPath, "materials/", ".mat" ) );
		try {
			CE2MaterialDB *	materials = nif->getCE2Materials();
			if ( materials )
				mat = materials->loadMaterial( fullPath );
		} catch ( std::exception & e ) {
			if ( std::string_view( e.what() ).starts_with( "BA2File: unexpected change to size of loose file" ) ) {
				Game::GameManager::GameResources &	r = nif->getGameResources();
				if ( r.ba2File && r.ba2File->findFile( fullPath ) )
					r.close_archives();
				if ( r.parent && r.parent->ba2File && r.parent->ba2File->findFile( fullPath ) )
					r.parent->close_archives();
				loadSFMaterial();
				return;
			}
			Message::append( nullptr, QString( "Error loading material(s)" ),
								QString( "'%1': %2" ).arg( fullPath.c_str() ).arg( e.what() ), QMessageBox::Critical );
		}
	}
	sfMaterialDB_ID = nif->getCE2MaterialDB_ID();
	sf_material = mat;
	if ( !sf_material )
		sf_material = createDefaultSFMaterial();
	else
		sf_material_valid = true;
	const_cast< NifModel * >( nif )->loadSFMaterial( iBlock, mat );
}

const CE2Material * BSShaderLightingProperty::createDefaultSFMaterial()
{
	CE2Material *	mat = sfMatDataBuf.constructObject< CE2Material >();
	if ( typeid( *this ) == typeid( BSEffectShaderProperty ) ) {
		mat->shaderRoute = 1;			// "Effect"
		CE2Material::EffectSettings *	sp = sfMatDataBuf.constructObject< CE2Material::EffectSettings >();
		sp->setFlags( CE2Material::EffectFlag_ZWrite, false );
		mat->effectSettings = sp;
		mat->flags = CE2Material::Flag_IsEffect | CE2Material::Flag_AlphaBlending | CE2Material::Flag_TwoSided;
	} else {
		mat->shaderModel = 11;			// "1LayerStandard"
		CE2Material::Layer *	l = sfMatDataBuf.constructObject< CE2Material::Layer >();
		CE2Material::Material *	m = sfMatDataBuf.constructObject< CE2Material::Material >();
		CE2Material::TextureSet *	t = sfMatDataBuf.constructObject< CE2Material::TextureSet >();
		t->textureReplacementMask = 0x09;	// color, roughness
		t->textureReplacements[0] = 0xFFFFFFFFU;
		t->textureReplacements[3] = 0xFF808080U;
		t->parent = m;
		m->textureSet = t;
		m->parent = l;
		l->material = m;
		l->parent = mat;
		mat->layers[0] = l;
		mat->layerMask = 0x01;
	}
	return mat;
}

bool BSShaderLightingProperty::bind( const QStringView & fname, bool forceTexturing, TexClampMode mode )
{
	GLuint mipmaps = scene->bindTexture( fname, forceTexturing );

	if ( !mipmaps )
		return false;

	static const GLint clampModes[14] = {
		GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE,	// CLAMP_S_CLAMP_T
		GL_CLAMP_TO_EDGE, GL_REPEAT,	// CLAMP_S_WRAP_T
		GL_REPEAT, GL_CLAMP_TO_EDGE,	// WRAP_S_CLAMP_T
		GL_REPEAT, GL_REPEAT,	// WRAP_S_WRAP_T
		GL_MIRRORED_REPEAT, GL_MIRRORED_REPEAT,	// MIRRORED_S_MIRRORED_T
		GL_CLAMP_TO_BORDER, GL_CLAMP_TO_BORDER,	// BORDER_S_BORDER_T
		GL_REPEAT, GL_REPEAT	// invalid, default to WRAP_S_WRAP_T
	};

	unsigned int i = std::min< unsigned int >( (unsigned int) mode, 6U ) << 1;
	GLint wrapS = clampModes[i];
	GLint wrapT = clampModes[i + 1];
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT );

	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, TexCache::get_max_anisotropy() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR );
	return true;
}

enum
{
	BGSM1_DIFFUSE = 0,
	BGSM1_NORMAL,
	BGSM1_SPECULAR,
	BGSM1_G2P,
	BGSM1_ENV,
	BGSM20_GLOW = 4,
	BGSM1_GLOW = 5,
	BGSM1_ENVMASK = 5,
	BGSM20_REFLECT,
	BGSM20_LIGHTING,

	BGSM1_MAX = 9,
	BGSM20_MAX = 10
};

QString BSShaderLightingProperty::fileName( int id ) const
{
	// Starfield (not implemented here)
	if ( bsVersion >= 170 )
		return QString();

	// Fallout 4 or 76 BGSM file
	if ( bsVersion >= 130 && material && material->isShaderMaterial() ) {
		// BSLSP
		auto m = static_cast<ShaderMaterial *>(material);
		if ( m->isValid() ) {
			auto tex = m->textures();
			if ( tex.count() >= BGSM1_MAX ) {
				switch ( id ) {
				case 0: // Diffuse
					if ( !tex[BGSM1_DIFFUSE].isEmpty() )
						return tex[BGSM1_DIFFUSE];
					break;
				case 1: // Normal
					if ( !tex[BGSM1_NORMAL].isEmpty() )
						return tex[BGSM1_NORMAL];
					break;
				case 2: // Glow
					if ( tex.count() == BGSM1_MAX && m->bGlowmap && !tex[BGSM1_GLOW].isEmpty() )
						return tex[BGSM1_GLOW];

					if ( tex.count() == BGSM20_MAX && m->bGlowmap && !tex[BGSM20_GLOW].isEmpty() )
						return tex[BGSM20_GLOW];
					break;
				case 3: // Greyscale
					if ( m->bGrayscaleToPaletteColor && !tex[BGSM1_G2P].isEmpty() )
						return tex[BGSM1_G2P];
					break;
				case 4: // Cubemap
					if ( tex.count() == BGSM1_MAX && m->bEnvironmentMapping && !tex[BGSM1_ENV].isEmpty() )
						return tex[BGSM1_ENV];
					break;
				case 5: // Env Mask
					if ( m->bEnvironmentMapping && !tex[BGSM1_ENVMASK].isEmpty() )
						return tex[BGSM1_ENVMASK];
					break;
				case 7: // Specular
					if ( m->bSpecularEnabled && !tex[BGSM1_SPECULAR].isEmpty() )
						return tex[BGSM1_SPECULAR];
					break;
				}
			}
			if ( tex.count() >= BGSM20_MAX ) {
				switch ( id ) {
				case 8:
					if ( m->bSpecularEnabled && !tex[BGSM20_REFLECT].isEmpty() )
						return tex[BGSM20_REFLECT];
					break;
				case 9:
					if ( m->bSpecularEnabled && !tex[BGSM20_LIGHTING].isEmpty() )
						return tex[BGSM20_LIGHTING];
					break;
				}
			}
		}

		if ( bsVersion < 151 )
			return QString();
	} else if ( bsVersion >= 130 && material && material->isEffectMaterial() ) {
		// From Fallout 4 or 76 effect material file
		auto m = static_cast<EffectMaterial*>(material);
		if ( m->isValid() ) {
			auto tex = m->textures();
			if ( id == 6 || id == 7 )
				id--;
			return tex[id];
		}

		return QString();
	}

	// From iTextureSet
	const NifModel * nif = NifModel::fromValidIndex(iTextureSet);
	if ( nif ) {
		if ( bsVersion >= 151 && ( id == 8 || id == 9 ) )
			id++;
		if ( id >= 0 && id < nif->get<int>(iTextureSet, "Num Textures") ) {
			QModelIndex iTextures = nif->getIndex(iTextureSet, "Textures");
			return nif->get<QString>( nif->getIndex( iTextures, id ) );
		}

		return QString();
	}

	// Handle niobject name="BSEffectShaderProperty...
	nif = NifModel::fromIndex( iBlock );
	if ( nif ) {
		switch ( id ) {
		case 0:
			return nif->get<QString>( iSPData, ( bsVersion >= 83 ? "Source Texture" : "File Name" ) );
		case 1:
			return nif->get<QString>( iSPData, "Greyscale Texture" );
		case 2:
			return nif->get<QString>( iSPData, "Env Map Texture" );
		case 3:
			return nif->get<QString>( iSPData, "Normal Texture" );
		case 4:
			return nif->get<QString>( iSPData, "Env Mask Texture" );
		case 6:
			return nif->get<QString>( iSPData, "Reflectance Texture" );
		case 7:
			return nif->get<QString>( iSPData, "Lighting Texture" );
		}
	}

	return QString();
}

int BSShaderLightingProperty::getId( const QString & id )
{
	const static QHash<QString, int> hash{
		{ "base",   0 },
		{ "dark",   1 },
		{ "detail", 2 },
		{ "gloss",  3 },
		{ "glow",   4 },
		{ "bumpmap", 5 },
		{ "decal0", 6 },
		{ "decal1", 7 }
	};

	return hash.value( id, -1 );
}

void BSShaderLightingProperty::setFlags1( const NifModel * nif )
{
	if ( bsVersion >= 151 ) {
		auto sf1 = nif->getArray<quint32>( iSPData, "SF1" );
		auto sf2 = nif->getArray<quint32>( iSPData, "SF2" );
		sf1.append( sf2 );

		uint64_t flags = 0;
		for ( auto sf : sf1 ) {
			flags |= ShaderFlags::CRC_TO_FLAG.value( sf, 0 );
		}
		flags1 = ShaderFlags::SF1( (uint32_t)flags );
	} else {
		flags1 = ShaderFlags::SF1( nif->get<unsigned int>(iSPData, "Shader Flags 1") );
	}
}

void BSShaderLightingProperty::setFlags2( const NifModel * nif )
{
	if ( bsVersion >= 151 ) {
		auto sf1 = nif->getArray<quint32>( iSPData, "SF1" );
		auto sf2 = nif->getArray<quint32>( iSPData, "SF2" );
		sf1.append( sf2 );

		uint64_t flags = 0;
		for ( auto sf : sf1 ) {
			flags |= ShaderFlags::CRC_TO_FLAG.value( sf, 0 );
		}
		flags2 = ShaderFlags::SF2( (uint32_t)(flags >> 32) );
	} else {
		flags2 = ShaderFlags::SF2( nif->get<unsigned int>(iSPData, "Shader Flags 2") );
	}
}

/*
	BSLightingShaderProperty
*/

void BSLightingShaderProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	BSShaderLightingProperty::updateImpl( nif, index );

	if ( index == iBlock ) {
		if ( bsVersion < 170 )
			setMaterial( nif, index, false );
		updateParams( nif );
	}
	else if ( index == iTextureSet ) {
		updateParams( nif );
	}
}

void BSLightingShaderProperty::resetParams()
{
	BSShaderLightingProperty::resetParams();

	hasGlowMap = false;
	hasEmittance = false;
	hasSoftlight = false;
	hasBacklight = false;
	hasRimlight = false;
	hasSpecularMap = false;
	hasMultiLayerParallax = false;
	hasEnvironmentMap = false;
	useEnvironmentMask = false;
	hasHeightMap = false;
	hasRefraction = false;
	hasDetailMask = false;
	hasTintMask = false;
	hasTintColor = false;
	greyscaleColor = false;

	emissiveColor = Color3(0, 0, 0);
	emissiveMult = 1.0;

	specularColor = Color3(0, 0, 0);
	specularGloss = 0;
	specularStrength = 0;

	tintColor = Color3(0, 0, 0);

	alpha = 1.0;

	lightingEffect1 = 0.0;
	lightingEffect2 = 1.0;

	// Multi-layer properties
	innerThickness = 1.0;
	innerTextureScale.reset();
	outerRefractionStrength = 0.0;
	outerReflectionStrength = 1.0;

	fresnelPower = 5.0;
	paletteScale = 1.0;
	rimPower = 2.0;
	backlightPower = 0.0;
}

void BSLightingShaderProperty::updateParams( const NifModel * nif )
{
	resetParams();

	if ( bsVersion >= 170 )
		return;

	setFlags1( nif );
	setFlags2( nif );

	if ( bsVersion >= 151 ) {
		shaderType = ShaderFlags::ShaderType::ST_EnvironmentMap;
		hasVertexAlpha = true;
		hasVertexColors = true;
	} else {
		shaderType = ShaderFlags::ShaderType( nif->get<unsigned int>(iBlock, "Shader Type") );
		hasVertexAlpha = hasSF1(ShaderFlags::SLSF1_Vertex_Alpha);
		hasVertexColors = hasSF2(ShaderFlags::SLSF2_Vertex_Colors);
	}
	isVertexAlphaAnimation = hasSF2(ShaderFlags::SLSF2_Tree_Anim);

	ShaderMaterial * m = ( material && material->isValid() ) ? static_cast<ShaderMaterial*>(material) : nullptr;
	if ( m ) {
		alpha = m->fAlpha;

		uvScale.set(m->fUScale, m->fVScale);
		uvOffset.set(m->fUOffset, m->fVOffset);

		specularColor = Color3(m->cSpecularColor);
		specularGloss = m->fSmoothness;
		specularStrength = m->fSpecularMult;

		emissiveColor = Color3(m->cEmittanceColor);
		emissiveMult = m->fEmittanceMult;

		if ( m->bTileU && m->bTileV )
			clampMode = TexClampMode::WRAP_S_WRAP_T;
		else if ( m->bTileU )
			clampMode = TexClampMode::WRAP_S_CLAMP_T;
		else if ( m->bTileV )
			clampMode = TexClampMode::CLAMP_S_WRAP_T;
		else
			clampMode = TexClampMode::CLAMP_S_CLAMP_T;

		fresnelPower = m->fFresnelPower;
		greyscaleColor = m->bGrayscaleToPaletteColor;
		paletteScale = m->fGrayscaleToPaletteScale;

		hasSpecularMap = m->bSpecularEnabled && (!m->textureList[2].isEmpty()
						|| (bsVersion >= 151 && !m->textureList[7].isEmpty()));
		hasGlowMap = m->bGlowmap;
		hasEmittance = m->bEmitEnabled;
		hasBacklight = m->bBackLighting;
		hasRimlight = m->bRimLighting;
		hasSoftlight = m->bSubsurfaceLighting;
		rimPower = m->fRimPower;
		backlightPower = m->fBacklightPower;
		isDoubleSided = m->bTwoSided;
		depthTest = m->bZBufferTest;
		depthWrite = m->bZBufferWrite;

		hasEnvironmentMap = m->bEnvironmentMapping || m->bPBR;
		useEnvironmentMask = hasEnvironmentMap && !m->bGlowmap && !m->textureList[5].isEmpty();
		environmentReflection = m->fEnvironmentMappingMaskScale;

		if ( hasSoftlight )
			lightingEffect1 = m->fSubsurfaceLightingRolloff;

		isVertexAlphaAnimation = m->bTree;

	} else { // m == nullptr

		auto textures = nif->getArray<QString>( iTextureSet, "Textures" );
		auto lsp = iSPData;

		isDoubleSided = hasSF2( ShaderFlags::SLSF2_Double_Sided );
		depthTest = hasSF1( ShaderFlags::SLSF1_ZBuffer_Test );
		depthWrite = hasSF2( ShaderFlags::SLSF2_ZBuffer_Write );

		alpha = nif->get<float>( lsp, "Alpha" );

		uvScale.set( nif->get<Vector2>(lsp, "UV Scale") );
		uvOffset.set( nif->get<Vector2>(lsp, "UV Offset") );
		clampMode = TexClampMode( nif->get<uint>( lsp, "Texture Clamp Mode" ) );

		// Specular
		if ( hasSF1( ShaderFlags::SLSF1_Specular ) ) {
			specularColor = nif->get<Color3>( lsp, "Specular Color" );
			specularGloss = nif->get<float>( lsp, "Glossiness" );
			if ( specularGloss == 0.0f ) // FO4
				specularGloss = nif->get<float>( lsp, "Smoothness" );
			specularStrength = nif->get<float>( lsp, "Specular Strength" );
		}

		// Emissive
		emissiveColor = nif->get<Color3>( lsp, "Emissive Color" );
		emissiveMult = nif->get<float>( lsp, "Emissive Multiple" );

		hasEmittance = hasSF1( ShaderFlags::SLSF1_Own_Emit );
		hasGlowMap = isST(ShaderFlags::ST_GlowShader) && hasSF2( ShaderFlags::SLSF2_Glow_Map ) && !textures.value( 2, "" ).isEmpty();

		// Version Dependent settings
		if ( bsVersion < 130 ) {
			lightingEffect1 = nif->get<float>( lsp, "Lighting Effect 1" );
			lightingEffect2 = nif->get<float>( lsp, "Lighting Effect 2" );

			innerThickness = nif->get<float>( lsp, "Parallax Inner Layer Thickness" );
			outerRefractionStrength = nif->get<float>( lsp, "Parallax Refraction Scale" );
			outerReflectionStrength = nif->get<float>( lsp, "Parallax Envmap Strength" );
			innerTextureScale.set( nif->get<Vector2>(lsp, "Parallax Inner Layer Texture Scale") );

			hasSpecularMap = hasSF1( ShaderFlags::SLSF1_Specular ) && !textures.value( 7, "" ).isEmpty();
			hasHeightMap = isST( ShaderFlags::ST_Heightmap ) && hasSF1( ShaderFlags::SLSF1_Parallax ) && !textures.value( 3, "" ).isEmpty();
			hasBacklight = hasSF2( ShaderFlags::SLSF2_Back_Lighting );
			hasRimlight = hasSF2( ShaderFlags::SLSF2_Rim_Lighting );
			hasSoftlight = hasSF2( ShaderFlags::SLSF2_Soft_Lighting );
			hasMultiLayerParallax = hasSF2( ShaderFlags::SLSF2_Multi_Layer_Parallax );
			hasRefraction = hasSF1( ShaderFlags::SLSF1_Refraction );

			hasTintMask = isST( ShaderFlags::ST_FaceTint );
			hasDetailMask = hasTintMask;

			if ( isST( ShaderFlags::ST_HairTint ) )
				setTintColor( nif, "Hair Tint Color" );
			else if ( isST( ShaderFlags::ST_SkinTint ) )
				setTintColor( nif, "Skin Tint Color" );
		} else {
			hasSpecularMap = hasSF1( ShaderFlags::SLSF1_Specular );
			greyscaleColor = hasSF1( ShaderFlags::SLSF1_Greyscale_To_PaletteColor );
			paletteScale = nif->get<float>( lsp, "Grayscale to Palette Scale" );
			lightingEffect1 = nif->get<float>( lsp, "Subsurface Rolloff" );
			backlightPower = nif->get<float>( lsp, "Backlight Power" );
			fresnelPower = nif->get<float>( lsp, "Fresnel Power" );
		}

		// Environment Map, Mask and Reflection Scale
		hasEnvironmentMap =
			( isST(ShaderFlags::ST_EnvironmentMap) && hasSF1(ShaderFlags::SLSF1_Environment_Mapping) )
			|| ( isST(ShaderFlags::ST_EyeEnvmap) && hasSF1(ShaderFlags::SLSF1_Eye_Environment_Mapping) )
			|| ( bsVersion == 100 && hasMultiLayerParallax );

		useEnvironmentMask = hasEnvironmentMap && !textures.value( 5, "" ).isEmpty();

		if ( isST( ShaderFlags::ST_EnvironmentMap ) )
			environmentReflection = nif->get<float>( lsp, "Environment Map Scale" );
		else if ( isST( ShaderFlags::ST_EyeEnvmap ) )
			environmentReflection = nif->get<float>( lsp, "Eye Cubemap Scale" );
	}
}

void BSLightingShaderProperty::setController( const NifModel * nif, const QModelIndex & iController )
{
	auto contrName = nif->itemName(iController);
	if ( contrName == "BSLightingShaderPropertyFloatController" ) {
		Controller * ctrl = new LightingFloatController( this, iController );
		registerController(nif, ctrl);
	} else if ( contrName == "BSLightingShaderPropertyColorController" ) {
		Controller * ctrl = new LightingColorController( this, iController );
		registerController(nif, ctrl);
	}
}

void BSLightingShaderProperty::setTintColor( const NifModel* nif, const QString & itemName )
{
	hasTintColor = true;
	tintColor = nif->get<Color3>(iSPData, itemName);
}

/*
	BSEffectShaderProperty
*/

void BSEffectShaderProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	BSShaderLightingProperty::updateImpl( nif, index );

	if ( index == iBlock ) {
		if ( bsVersion < 83 )
			return;
		if ( bsVersion < 170 )
			setMaterial( nif, index, true );
		updateParams( nif );
	} else if ( index == iTextureSet ) {
		updateParams( nif );
	}
}

void BSEffectShaderProperty::resetParams()
{
	BSShaderLightingProperty::resetParams();

	hasSourceTexture = false;
	hasGreyscaleMap = false;
	hasEnvironmentMap = false;
	hasEnvironmentMask = false;
	hasNormalMap = false;
	useFalloff = false;
	hasRGBFalloff = false;

	greyscaleColor = false;
	greyscaleAlpha = false;

	hasWeaponBlood = false;

	falloff.startAngle = 1.0f;
	falloff.stopAngle = 0.0f;
	falloff.startOpacity = 1.0f;
	falloff.stopOpacity = 0.0f;
	falloff.softDepth = 1.0f;

	lumEmittance = 0.0;

	emissiveColor = Color4(0, 0, 0, 0);
	emissiveMult = 1.0;

	lightingInfluence = 0.0;
}

void BSEffectShaderProperty::updateParams( const NifModel * nif )
{
	resetParams();

	setFlags1( nif );
	setFlags2( nif );

	hasVertexAlpha = hasSF1( ShaderFlags::SLSF1_Vertex_Alpha );
	hasVertexColors = hasSF2( ShaderFlags::SLSF2_Vertex_Colors );
	isVertexAlphaAnimation = hasSF2(ShaderFlags::SLSF2_Tree_Anim);

	EffectMaterial * m = ( material && material->isValid() ) ? static_cast<EffectMaterial*>(material) : nullptr;
	if ( m ) {
		hasSourceTexture = !m->textureList[0].isEmpty();
		hasGreyscaleMap = !m->textureList[1].isEmpty();
		hasEnvironmentMap = !m->textureList[2].isEmpty();
		hasNormalMap = !m->textureList[3].isEmpty();
		hasEnvironmentMask = !m->textureList[4].isEmpty();

		environmentReflection = m->fEnvironmentMappingMaskScale;

		greyscaleAlpha = m->bGrayscaleToPaletteAlpha;
		greyscaleColor = m->bGrayscaleToPaletteColor;
		useFalloff = m->bFalloffEnabled;
		hasRGBFalloff = m->bFalloffColorEnabled;

		depthTest = m->bZBufferTest;
		depthWrite = m->bZBufferWrite;
		isDoubleSided = m->bTwoSided;

		lumEmittance = m->fLumEmittance;

		uvScale.set(m->fUScale, m->fVScale);
		uvOffset.set(m->fUOffset, m->fVOffset);

		if ( m->bTileU && m->bTileV )
			clampMode = TexClampMode::WRAP_S_WRAP_T;
		else if ( m->bTileU )
			clampMode = TexClampMode::WRAP_S_CLAMP_T;
		else if ( m->bTileV )
			clampMode = TexClampMode::CLAMP_S_WRAP_T;
		else
			clampMode = TexClampMode::CLAMP_S_CLAMP_T;

		emissiveColor = Color4(m->cBaseColor, m->fAlpha);
		emissiveMult = m->fBaseColorScale;

		if ( m->bEffectLightingEnabled )
			lightingInfluence = m->fLightingInfluence;

		falloff.startAngle = m->fFalloffStartAngle;
		falloff.stopAngle = m->fFalloffStopAngle;
		falloff.startOpacity = m->fFalloffStartOpacity;
		falloff.stopOpacity = m->fFalloffStopOpacity;
		falloff.softDepth = m->fSoftDepth;

	} else { // m == nullptr

		auto esp = iSPData;

		hasSourceTexture = !nif->get<QString>( esp, "Source Texture" ).isEmpty();
		hasGreyscaleMap = !nif->get<QString>( esp, "Greyscale Texture" ).isEmpty();

		greyscaleAlpha = hasSF1( ShaderFlags::SLSF1_Greyscale_To_PaletteAlpha );
		greyscaleColor = hasSF1( ShaderFlags::SLSF1_Greyscale_To_PaletteColor );
		useFalloff = hasSF1( ShaderFlags::SLSF1_Use_Falloff );

		depthTest = hasSF1( ShaderFlags::SLSF1_ZBuffer_Test );
		depthWrite = hasSF2( ShaderFlags::SLSF2_ZBuffer_Write );
		isDoubleSided = hasSF2( ShaderFlags::SLSF2_Double_Sided );

		if ( bsVersion < 130 ) {
			hasWeaponBlood = hasSF2( ShaderFlags::SLSF2_Weapon_Blood );
		} else {
			hasEnvironmentMap = !nif->get<QString>( esp, "Env Map Texture" ).isEmpty();
			hasEnvironmentMask = !nif->get<QString>( esp, "Env Mask Texture" ).isEmpty();
			hasNormalMap = !nif->get<QString>( esp, "Normal Texture" ).isEmpty();

			environmentReflection = nif->get<float>( esp, "Environment Map Scale" );

			// Receive Shadows -> RGB Falloff for FO4
			hasRGBFalloff = hasSF1( ShaderFlags::SF1( 1 << 8 ) );
		}

		uvScale.set( nif->get<Vector2>(esp, "UV Scale") );
		uvOffset.set( nif->get<Vector2>(esp, "UV Offset") );
		clampMode = TexClampMode( nif->get<quint8>( esp, "Texture Clamp Mode" ) );

		emissiveColor = nif->get<Color4>( esp, "Base Color" );
		emissiveMult = nif->get<float>( esp, "Base Color Scale" );

		if ( hasSF2( ShaderFlags::SLSF2_Effect_Lighting ) )
			lightingInfluence = (float)nif->get<quint8>( esp, "Lighting Influence" ) / 255.0;

		falloff.startAngle = nif->get<float>( esp, "Falloff Start Angle" );
		falloff.stopAngle = nif->get<float>( esp, "Falloff Stop Angle" );
		falloff.startOpacity = nif->get<float>( esp, "Falloff Start Opacity" );
		falloff.stopOpacity = nif->get<float>( esp, "Falloff Stop Opacity" );
		falloff.softDepth = nif->get<float>( esp, "Soft Falloff Depth" );
	}
}

void BSEffectShaderProperty::setController( const NifModel * nif, const QModelIndex & iController )
{
	auto contrName = nif->itemName(iController);
	if ( contrName == "BSEffectShaderPropertyFloatController" ) {
		Controller * ctrl = new EffectFloatController( this, iController );
		registerController(nif, ctrl);
	} else if ( contrName == "BSEffectShaderPropertyColorController" ) {
		Controller * ctrl = new EffectColorController( this, iController );
		registerController(nif, ctrl);
	}
}

/*
	BSWaterShaderProperty
*/

unsigned int BSWaterShaderProperty::getWaterShaderFlags() const
{
	return (unsigned int)waterShaderFlags;
}

void BSWaterShaderProperty::setWaterShaderFlags( unsigned int val )
{
	waterShaderFlags = WaterShaderFlags::SF1( val );
}


namespace ShaderFlags
{
	const QMap<uint, uint64_t> CRC_TO_FLAG = {
		// SF1
		{PBR, SLSF1_Specular},
		{CAST_SHADOWS, SLSF1_Cast_Shadows},
		{ZBUFFER_TEST, SLSF1_ZBuffer_Test},
		{SKINNED, SLSF1_Skinned},
		{ENVMAP, SLSF1_Environment_Mapping},
		{VERTEX_ALPHA, SLSF1_Vertex_Alpha},
		{FACE, SLSF1_Facegen_Detail_Map},
		{GRAYSCALE_TO_PALETTE_COLOR, SLSF1_Greyscale_To_PaletteColor},
		{GRAYSCALE_TO_PALETTE_ALPHA, SLSF1_Greyscale_To_PaletteAlpha},
		{DECAL, SLSF1_Decal},
		{DYNAMIC_DECAL, SLSF1_Dynamic_Decal},
		{EMIT_ENABLED, SLSF1_Own_Emit},
		{REFRACTION, SLSF1_Refraction},
		{SKIN_TINT, SLSF1_FaceGen_RGB_Tint},
		{RGB_FALLOFF, SLSF1_Receive_Shadows},
		{EXTERNAL_EMITTANCE, SLSF1_External_Emittance},
		{MODELSPACENORMALS, SLSF1_Model_Space_Normals},
		{FALLOFF, SLSF1_Use_Falloff},
		{SOFT_EFFECT, SLSF1_Soft_Effect},
		// SF2
		{ZBUFFER_WRITE, (uint64_t)SLSF2_ZBuffer_Write << 32},
		{GLOWMAP, (uint64_t)SLSF2_Glow_Map << 32},
		{TWO_SIDED, (uint64_t)SLSF2_Double_Sided << 32},
		{VERTEXCOLORS, (uint64_t)SLSF2_Vertex_Colors << 32},
		{NOFADE, (uint64_t)SLSF2_No_Fade << 32},
		{WEAPON_BLOOD, (uint64_t)SLSF2_Weapon_Blood << 32},
		{TRANSFORM_CHANGED, (uint64_t)SLSF2_Assume_Shadowmask << 32},
		{EFFECT_LIGHTING, (uint64_t)SLSF2_Effect_Lighting << 32},
		{LOD_OBJECTS, (uint64_t)SLSF2_LOD_Objects << 32},

		// TODO
		{REFRACTION_FALLOFF, 0},
		{INVERTED_FADE_PATTERN, 0},
		{HAIRTINT, 0},
		{NO_EXPOSURE, 0},
	};
}
