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

#include "nifmodel.h"

#include "xml/xmlconfig.h"
#include "message.h"
#include "spellbook.h"
#include "data/niftypes.h"
#include "io/nifstream.h"
#include "material.hpp"
#include "gamemanager.h"

#include <QByteArray>
#include <QColor>
#include <QDebug>
#include <QFile>
#include <QSettings>
#include <QStringBuilder>

void NifModel::loadSFBlender( NifItem * parent, const void * o, const void * layerUVStream )
{
	const char *	name = "";
	const char *	maskTexture = "";
	bool	maskTextureReplacementEnabled = false;
	std::uint32_t	maskTextureReplacement = 0xFFFFFFFFU;
	const CE2Material::UVStream *	maskUVStream = reinterpret_cast< const CE2Material::UVStream * >(layerUVStream);
	unsigned char	blendMode = 0;
	unsigned char	vertexColorChannel = 0;
	float	floatParams[5];
	bool	boolParams[8];
	for ( int i = 0; i < 5; i++ )
		floatParams[i] = 0.5f;
	for ( int i = 0; i < 8; i++ )
		boolParams[i] = false;
	if ( o ) {
		const CE2Material::Blender *	blender = reinterpret_cast< const CE2Material::Blender * >(o);
		name = blender->name->c_str();
		maskTexture = blender->texturePath->c_str();
		maskTextureReplacementEnabled = blender->textureReplacementEnabled;
		maskTextureReplacement = blender->textureReplacement;
		if ( blender->uvStream )
			maskUVStream = blender->uvStream;
		blendMode = blender->blendMode;
		vertexColorChannel = blender->colorChannel;
		for ( int i = 0; i < 5; i++ )
			floatParams[i] = blender->floatParams[i];
		for ( int i = 0; i < 8; i++ )
			boolParams[i] = blender->boolParams[i];
	}
	if ( parent ) {
		setValue<QString>( parent, "Name", name );
		loadSFUVStream( getItem( itemToIndex( parent ), "UV Stream" ), maskUVStream );
		loadSFTextureWithReplacement( getItem( itemToIndex( parent ), "Mask Texture" ), maskTexture, maskTextureReplacementEnabled, maskTextureReplacement );
		setValue<quint8>( parent, "Blend Mode", blendMode );
		setValue<quint8>( parent, "Vertex Color Channel", vertexColorChannel );
		for ( int i = 0; i < 5; i++ )
			setValue<float>( parent, QString("Float Param %1").arg(i), floatParams[i] );
		for ( int i = 0; i < 8; i++ )
			setValue<bool>( parent, QString("Bool Param %1").arg(i), boolParams[i] );
	}
}

void NifModel::loadSFLayer( NifItem * parent, const void * o )
{
	const char *	name = "";
	const CE2Material::UVStream *	uvStream = nullptr;
	const CE2Material::Material *	material = nullptr;
	if ( o ) {
		const CE2Material::Layer *	layer = reinterpret_cast< const CE2Material::Layer * >(o);
		name = layer->name->c_str();
		uvStream = layer->uvStream;
		material = layer->material;
	}
	if ( parent ) {
		setValue<QString>( parent, "Name", name );
		loadSFUVStream( getItem( itemToIndex( parent ), QString("UV Stream") ), uvStream );
		loadSFMaterial( getItem( itemToIndex( parent ), QString("Material") ), material );
	}
}

void NifModel::loadSFMaterial( NifItem * parent, const void * o )
{
	const char *	name = "";
	FloatVector4	color(1.0f);
	unsigned char	colorMode = 0;
	const CE2Material::TextureSet *	textureSet = nullptr;
	if ( o ) {
		const CE2Material::Material * material = reinterpret_cast< const CE2Material::Material * >(o);
		name = material->name->c_str();
		color = material->color;
		colorMode = material->colorMode;
		textureSet = material->textureSet;
	}
	if ( parent ) {
		setValue<QString>( parent, "Name", name );
		setValue<Color4>( parent, "Color", Color4(color[0], color[1], color[2], color[3]) );
		setValue<quint8>( parent, "Color Override Mode", colorMode );
		loadSFTextureSet( getItem( itemToIndex( parent ), QString("Texture Set") ), textureSet );
	}
}

void NifModel::loadSFTextureWithReplacement( NifItem * parent, const char * texturePath, bool replacementEnabled, std::uint32_t replacementColor )
{
	if ( parent ) {
		if ( !( texturePath && *texturePath ) )
			setValue<QString>( parent, "Path", QString() );
		else
			setValue<QString>( parent, "Path", texturePath );
		setValue<bool>( parent, "Replacement Enabled", replacementEnabled );
		if ( replacementEnabled )
			setValue<ByteColor4>( parent, "Replacement Color", ByteColor4(replacementColor) );
	}
}

void NifModel::loadSFTextureSet( NifItem * parent, const void * o )
{
	if ( !parent )
		return;
	const char *	name = "";
	float	floatParam = 0.5f;
	unsigned char	resolutionHint = 0;
	std::uint32_t	texturePathMask = 0;
	std::uint32_t	textureReplacementMask = 0;
	const CE2Material::TextureSet *	textureSet = reinterpret_cast< const CE2Material::TextureSet * >(o);
	if ( o ) {
		name = textureSet->name->c_str();
		floatParam = textureSet->floatParam;
		resolutionHint = textureSet->resolutionHint;
		texturePathMask = textureSet->texturePathMask;
		textureReplacementMask = textureSet->textureReplacementMask;
	}
	std::uint32_t	textureEnableMask = texturePathMask | textureReplacementMask;
	setValue<QString>( parent, "Name", name );
	setValue<float>( parent, "Float Param", floatParam );
	setValue<quint8>( parent, "Resolution Hint", resolutionHint );
	setValue<quint32>( parent, "Enable Mask", textureEnableMask );
	for ( int i = 0; textureEnableMask; i++, textureEnableMask = textureEnableMask >> 1 ) {
		if ( !(textureEnableMask & 1) )
			continue;
		NifItem *	t = getItem( itemToIndex( parent ), QString("Texture %1").arg(i) );
		const char *	texturePath = nullptr;
		bool	replacementEnabled = bool( textureReplacementMask & (1 << i) );
		std::uint32_t	replacementColor = 0;
		if ( texturePathMask & (1 << i) )
			texturePath = textureSet->texturePaths[i]->c_str();
		if ( replacementEnabled )
			replacementColor = textureSet->textureReplacements[i];
		loadSFTextureWithReplacement( t, texturePath, replacementEnabled, replacementColor );
	}
}

void NifModel::loadSFUVStream( NifItem * parent, const void * o, const void * p )
{
	const char *	name = "";
	FloatVector4	scaleAndOffset( 1.0f, 1.0f, 0.0f, 0.0f );
	unsigned char	textureAddressMode = 0;
	unsigned char	channel = 1;
	if ( !o )
		o = p;
	if ( o ) {
		const CE2Material::UVStream *	uvStream = reinterpret_cast< const CE2Material::UVStream * >(o);
		name = uvStream->name->c_str();
		scaleAndOffset = uvStream->scaleAndOffset;
		textureAddressMode = uvStream->textureAddressMode;
		channel = uvStream->channel;
	}
	if ( parent ) {
		setValue<QString>( parent, "Name", name );
		setValue<float>( parent, "U Scale", scaleAndOffset[0] );
		setValue<float>( parent, "V Scale", scaleAndOffset[1] );
		setValue<float>( parent, "U Offset", scaleAndOffset[2] );
		setValue<float>( parent, "V Offset", scaleAndOffset[3] );
		setValue<quint8>( parent, "Texture Address Mode", textureAddressMode );
		setValue<quint8>( parent, "Channel", channel );
	}
}

void NifModel::loadSFMaterial( const QModelIndex & parent, int lodLevel )
{
	NifItem *	p = getItem( parent, false );
	if ( !p )
		return;
	NifItem *	m = p;
	if ( p->name() != "Material" )
		m = getItem( itemToIndex( p ), "Material" );
	else
		p = getItem( this->parent( parent ), false );

	const CE2MaterialDB *	materials = Game::GameManager::materials( Game::STARFIELD );
	const CE2Material *	material = nullptr;
	if ( materials ) {
		std::string	path = get<QString>( p, "Name" ).toStdString();
		if ( !path.empty() ) {
			for ( size_t i = 0; i < path.length(); i++ ) {
				char	c = path[i];
				if ( c >= 'A' && c <= 'Z' )
					c += ( 'a' - 'A' );
				else if ( c == '\\' )
					c = '/';
				path[i] = c;
			}
			if ( !path.starts_with( "materials/" ) ) {
				size_t	n = path.find( "/materials/" );
				if ( n != std::string::npos )
					path.erase( 0, n + 1 );
				else
					path.insert( 0, "materials/" );
			}
			if ( !path.ends_with( ".mat" ) ) {
				if ( path.ends_with( ".bgsm" ) || path.ends_with( ".bgem" ) )
					path.resize( path.length() - 5 );
				path += ".mat";
			}
			material = materials->findMaterial( path );
		}
	}

	if ( lodLevel > 0 && material ) {
		for ( int i = lodLevel - 1; i >= 0; i-- ) {
			if ( i <= 2 && material->lodMaterials[i] ) {
				material = material->lodMaterials[i];
				break;
			}
		}
	}

	setValue<QString>( m, "Name", ( material ? material->name->c_str() : "" ) );
	for ( int l = 0; l < 6; l++ ) {
		bool	layerEnabled = false;
		if ( material )
			layerEnabled = bool( material->layerMask & (1 << l) );
		setValue<bool>( m, QString("Layer %1 Enabled").arg(l), layerEnabled );
		if ( layerEnabled ) {
			loadSFLayer( getItem( itemToIndex( m ), QString("Layer %1").arg(l) ), material->layers[l] );
			if ( l > 0 && material->blenders[l - 1] )
				loadSFBlender( getItem( itemToIndex( m ), QString("Blender %1").arg(l - 1) ), material->blenders[l - 1], material->layers[l]->uvStream );
		}
	}
	setValue<QString>( m, "Shader Model", QString( material ? CE2Material::shaderModelNames[material->shaderModel] : "" ) );
	setValue<quint8>( m, "Shader Route", ( material ? material->shaderRoute : 0 ) );
	setValue<bool>( m, "Two Sided", ( material ? bool(material->flags & CE2Material::Flag_TwoSided) : false ) );
	bool	hasOpacity = false;
	if ( material )
		hasOpacity = bool( material->flags & CE2Material::Flag_HasOpacity );
	setValue<bool>( m, "Has Opacity", hasOpacity );
	if ( hasOpacity ) {
	}
	bool	isEffect = false;
	if ( material )
		isEffect = bool( material->flags & CE2Material::Flag_IsEffect );
	setValue<bool>( m, "Is Effect", isEffect );
	if ( isEffect ) {
	}
	bool	isDecal = false;
	if ( material )
		isDecal = bool( material->flags & CE2Material::Flag_IsDecal );
	setValue<bool>( m, "Is Decal", isDecal );
	if ( isDecal ) {
	}
	bool	isWater = false;
	if ( material )
		isWater = bool( material->flags & CE2Material::Flag_IsWater );
	setValue<bool>( m, "Is Water", isWater );
	if ( isWater ) {
	}
	bool	isEmissive = false;
	if ( material )
		isEmissive = bool( material->flags & CE2Material::Flag_Emissive );
	setValue<bool>( m, "Is Emissive", isEmissive );
	if ( isEmissive ) {
	}
	bool	isTranslucent = false;
	if ( material )
		isTranslucent = bool( material->flags & CE2Material::Flag_Translucency );
	setValue<bool>( m, "Is Translucent", isTranslucent );
	if ( isTranslucent ) {
	}
}

void NifModel::loadBGSMMaterial( const QModelIndex & parent )
{
	NifItem *	p = getItem( parent, false );
	if ( !p )
		return;
}

void NifModel::loadBGEMMaterial( const QModelIndex & parent )
{
	NifItem *	p = getItem( parent, false );
	if ( !p )
		return;
}

void NifModel::loadMeshFiles( const QModelIndex & parent )
{
	NifItem *	p = getItem( parent, false );
	if ( !p )
		return;
}
