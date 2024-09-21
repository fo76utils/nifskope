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
#include "io/material.h"

#include <QByteArray>
#include <QColor>
#include <QDebug>
#include <QFile>
#include <QSettings>
#include <QStringBuilder>

void NifModel::loadSFBlender( NifItem * parent, const void * o )
{
	const char *	name = "";
	const char *	maskTexture = "";
	bool	maskTextureReplacementEnabled = false;
	std::uint32_t	maskTextureReplacement = 0xFFFFFFFFU;
	const CE2Material::UVStream *	maskUVStream = nullptr;
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
		name = blender->name;
		maskTexture = blender->texturePath->data();
		maskTextureReplacementEnabled = blender->textureReplacementEnabled;
		maskTextureReplacement = blender->textureReplacement;
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
		loadSFUVStream( getItem( parent, "UV Stream" ), maskUVStream );
		loadSFTextureWithReplacement( getItem( parent, "Mask Texture" ), maskTexture, maskTextureReplacementEnabled, maskTextureReplacement );
		setValue<quint8>( parent, "Blend Mode", blendMode );
		setValue<quint8>( parent, "Vertex Color Channel", vertexColorChannel );
		setValue<float>( parent, "Height Blend Threshold", floatParams[0] );
		setValue<float>( parent, "Height Blend Factor", floatParams[1] );
		setValue<float>( parent, "Position", floatParams[2] );
		setValue<float>( parent, "Contrast", 1.0f - floatParams[3] );
		setValue<float>( parent, "Mask Intensity", floatParams[4] );
		setValue<bool>( parent, "Blend Color", boolParams[0] );
		setValue<bool>( parent, "Blend Metalness", boolParams[1] );
		setValue<bool>( parent, "Blend Roughness", boolParams[2] );
		setValue<bool>( parent, "Blend Normals", boolParams[3] );
		setValue<bool>( parent, "Blend Normals Additively", boolParams[4] );
		setValue<bool>( parent, "Use Vertex Color", boolParams[5] );
		setValue<bool>( parent, "Blend Ambient Occlusion", boolParams[6] );
		setValue<bool>( parent, "Use Detail Blend Mask", boolParams[7] );
	}
}

void NifModel::loadSFLayer( NifItem * parent, const void * o )
{
	const char *	name = "";
	const CE2Material::UVStream *	uvStream = nullptr;
	const CE2Material::Material *	material = nullptr;
	if ( o ) {
		const CE2Material::Layer *	layer = reinterpret_cast< const CE2Material::Layer * >(o);
		name = layer->name;
		uvStream = layer->uvStream;
		material = layer->material;
	}
	if ( parent ) {
		setValue<QString>( parent, "Name", name );
		loadSFUVStream( getItem( parent, QString("UV Stream") ), uvStream );
		loadSFMaterial( getItem( parent, QString("Material") ), material );
	}
}

void NifModel::loadSFMaterial( NifItem * parent, const void * o )
{
	const char *	name = "";
	FloatVector4	color(1.0f);
	unsigned char	colorMode = 0;
	bool	isFlipbook = false;
	const CE2Material::TextureSet *	textureSet = nullptr;
	const CE2Material::Material * material = reinterpret_cast< const CE2Material::Material * >(o);
	if ( o ) {
		name = material->name;
		color = material->color;
		colorMode = material->colorModeFlags;
		isFlipbook = bool(material->flipbookFlags & 1);
		textureSet = material->textureSet;
	}
	if ( parent ) {
		setValue<QString>( parent, "Name", name );
		setValue<Color4>( parent, "Color", Color4(color[0], color[1], color[2], color[3]) );
		setValue<quint8>( parent, "Color Override Mode", colorMode );
		setValue<bool>( parent, "Is Flipbook", isFlipbook );
		if ( isFlipbook ) {
			setValue<quint8>( parent, "Flipbook Columns", material->flipbookColumns );
			setValue<quint8>( parent, "Flipbook Rows", material->flipbookRows );
			setValue<float>( parent, "Flipbook FPS", material->flipbookFPS );
			setValue<bool>( parent, "Flipbook Loops", bool(material->flipbookFlags & 2) );
		}
		loadSFTextureSet( getItem( parent, QString("Texture Set") ), textureSet );
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
	float	floatParam = 1.0f;
	unsigned char	resolutionHint = 0;
	bool			disableMipBiasHint = false;
	std::uint32_t	texturePathMask = 0;
	std::uint32_t	textureReplacementMask = 0;
	const CE2Material::TextureSet *	textureSet = reinterpret_cast< const CE2Material::TextureSet * >(o);
	if ( o ) {
		name = textureSet->name;
		floatParam = textureSet->floatParam;
		resolutionHint = textureSet->resolutionHint;
		disableMipBiasHint = textureSet->disableMipBiasHint;
		texturePathMask = textureSet->texturePathMask;
		textureReplacementMask = textureSet->textureReplacementMask;
	}
	std::uint32_t	textureEnableMask = texturePathMask | textureReplacementMask;
	setValue<QString>( parent, "Name", name );
	setValue<float>( parent, "Normal Intensity", floatParam );
	setValue<quint32>( parent, "Enable Mask", textureEnableMask );
	for ( int i = 0; textureEnableMask; i++, textureEnableMask = textureEnableMask >> 1 ) {
		if ( !(textureEnableMask & 1) )
			continue;
		NifItem *	t = getItem( parent, QString("Texture %1").arg(i) );
		const char *	texturePath = nullptr;
		bool	replacementEnabled = bool( textureReplacementMask & (1 << i) );
		std::uint32_t	replacementColor = 0;
		if ( texturePathMask & (1 << i) )
			texturePath = textureSet->texturePaths[i]->data();
		if ( replacementEnabled )
			replacementColor = textureSet->textureReplacements[i];
		loadSFTextureWithReplacement( t, texturePath, replacementEnabled, replacementColor );
	}
	setValue<quint8>( parent, "Resolution Hint", resolutionHint );
	setValue<bool>( parent, "Disable Mip Bias Hint", disableMipBiasHint );
}

void NifModel::loadSFUVStream( NifItem * parent, const void * o )
{
	const char *	name = "";
	FloatVector4	scaleAndOffset( 1.0f, 1.0f, 0.0f, 0.0f );
	unsigned char	textureAddressMode = 0;
	unsigned char	channel = 1;
	if ( o ) {
		const CE2Material::UVStream *	uvStream = reinterpret_cast< const CE2Material::UVStream * >(o);
		name = uvStream->name;
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

void NifModel::loadSFMaterial( const QModelIndex & parent, const void *matPtr, int lodLevel )
{
	NifItem *	p = getItem( parent, false );
	if ( !p )
		return;
	NifItem *	m = p;
	if ( !p->hasName( "Material" ) )
		m = getItem( p, "Material" );
	else
		p = getItem( this->parent( parent ), false );
	std::string	path( Game::GameManager::get_full_path( get<QString>( p, "Name" ), "materials/", ".mat" ) );
	if ( p ) {
		// calculate and set material ID
		std::uint32_t	materialID = 0U;
		for ( size_t i = 0; i < path.length(); i++ ) {
			char	c = path[i];
			hashFunctionCRC32( materialID, (unsigned char) ( c != '/' ? c : '\\' ) );
		}
		int	parentBlock = getParent( itemToIndex( p ) );
		if ( parentBlock >= 0 && blockInherits( getBlockItem( parentBlock ), "BSGeometry" ) ) {
			auto	links = getChildLinks( parentBlock );
			for ( const auto link : links ) {
				auto	idx = getBlockIndex( link );
				if ( blockInherits( idx, "NiIntegerExtraData" ) )
					setValue<quint32>( getItem( idx ), "Integer Data", materialID );
			}
		}
	}
	if ( !m )
		return;
	for ( auto c : m->childIter() )
		c->invalidateCondition();

	const CE2Material *	material = reinterpret_cast< const CE2Material * >( matPtr );

	if ( lodLevel > 0 && material ) {
		for ( int i = lodLevel - 1; i >= 0; i-- ) {
			if ( i <= 2 && material->lodMaterials[i] ) {
				material = material->lodMaterials[i];
				break;
			}
		}
	}

	setValue<QString>( m, "Name", ( material ? material->name : "" ) );
	setValue<bool>( m, "Is Modified", false );
	for ( int l = 0; l < CE2Material::maxLayers; l++ ) {
		bool	layerEnabled = false;
		if ( material )
			layerEnabled = bool( material->layerMask & (1 << l) );
		setValue<bool>( m, QString("Layer %1 Enabled").arg(l), layerEnabled );
		if ( layerEnabled ) {
			loadSFLayer( getItem( m, QString("Layer %1").arg(l) ), material->layers[l] );
			if ( l > 0 && material->blenders[l - 1] )
				loadSFBlender( getItem( m, QString("Blender %1").arg(l - 1) ), material->blenders[l - 1] );
		}
	}
	setValue<QString>( m, "Shader Model", QString( material ? CE2Material::shaderModelNames[material->shaderModel] : "BaseMaterial" ) );
	setValue<quint8>( m, "Shader Route", ( material ? material->shaderRoute : 0 ) );
	setValue<bool>( m, "Two Sided", ( material ? bool(material->flags & CE2Material::Flag_TwoSided) : false ) );
	setValue<quint8>( m, "Physics Material Type", ( material ? material->physicsMaterialType : 0 ) );
	NifItem *	o;
	if ( material && material->shaderRoute == 1 ) {	// effect material
		bool	hasOpacityComponent = bool( material->flags & CE2Material::Flag_HasOpacityComponent );
		setValue<bool>( m, "Has Opacity Component", hasOpacityComponent );
		if ( hasOpacityComponent && ( o = getItem( m, "Opacity Settings" ) ) != nullptr ) {
			setValue<quint8>( o, "First Layer Index", material->opacityLayer1 );
			setValue<bool>( o, "Second Layer Active", bool( material->flags & CE2Material::Flag_OpacityLayer2Active ) );
			setValue<quint8>( o, "Second Layer Index", material->opacityLayer2 );
			setValue<quint8>( o, "First Blender Index", material->opacityBlender1 );
			setValue<quint8>( o, "First Blender Mode", material->opacityBlender1Mode );
			setValue<bool>( o, "Third Layer Active", bool( material->flags & CE2Material::Flag_OpacityLayer3Active ) );
			setValue<quint8>( o, "Third Layer Index", material->opacityLayer3 );
			setValue<quint8>( o, "Second Blender Index", material->opacityBlender2 );
			setValue<quint8>( o, "Second Blender Mode", material->opacityBlender2Mode );
			setValue<float>( o, "Specular Opacity Override", material->specularOpacityOverride );
		}
	} else {
		bool	hasOpacity = false;
		if ( material )
			hasOpacity = bool( material->flags & CE2Material::Flag_HasOpacity );
		setValue<bool>( m, "Has Opacity", hasOpacity );
		if ( hasOpacity && ( o = getItem( m, "Alpha Settings" ) ) != nullptr ) {
			setValue<float>( o, "Alpha Test Threshold", material->alphaThreshold );
			setValue<quint8>( o, "Opacity Source Layer", material->alphaSourceLayer );
			setValue<quint8>( o, "Alpha Blender Mode", material->alphaBlendMode );
			setValue<bool>( o, "Use Detail Blend Mask", bool(material->flags & CE2Material::Flag_AlphaDetailBlendMask) );
			setValue<bool>( o, "Use Vertex Color", bool(material->flags & CE2Material::Flag_AlphaVertexColor) );
			if ( material->flags & CE2Material::Flag_AlphaVertexColor )
				setValue<quint8>( o, "Vertex Color Channel", material->alphaVertexColorChannel );
			loadSFUVStream( getItem( o, "Opacity UV Stream" ), material->alphaUVStream );
			setValue<float>( o, "Height Blend Threshold", material->alphaHeightBlendThreshold );
			setValue<float>( o, "Height Blend Factor", material->alphaHeightBlendFactor );
			setValue<float>( o, "Position", material->alphaPosition );
			setValue<float>( o, "Contrast", material->alphaContrast );
			setValue<bool>( o, "Use Dithered Transparency", bool(material->flags & CE2Material::Flag_DitheredTransparency) );
		}
	}
	bool	useDetailBlendMask = false;
	if ( material )
		useDetailBlendMask = bool( material->flags & CE2Material::Flag_UseDetailBlender );
	setValue<bool>( m, "Detail Blend Mask Supported", useDetailBlendMask );
	if ( useDetailBlendMask && ( o = getItem( m, "Detail Blender Settings" ) ) != nullptr ) {
		const CE2Material::DetailBlenderSettings *	sp = material->detailBlenderSettings;
		loadSFTextureWithReplacement( getItem( o, "Texture" ), sp->texturePath->data(), sp->textureReplacementEnabled, sp->textureReplacement );
		loadSFUVStream( getItem( o, "UV Stream" ), sp->uvStream );
	}
	bool	isEffect = false;
	if ( material )
		isEffect = bool( material->flags & CE2Material::Flag_IsEffect );
	setValue<bool>( m, "Is Effect", isEffect );
	if ( isEffect && ( o = getItem( m, "Effect Settings" ) ) != nullptr ) {
		const CE2Material::EffectSettings *	sp = material->effectSettings;
		for ( NifItem * q = getItem( o, "Falloff Settings (Deprecated)" ); q; ) {
			setValue<bool>( q, "Use Falloff", bool(sp->flags & CE2Material::EffectFlag_UseFalloff) );
			setValue<bool>( q, "Use RGB Falloff", bool(sp->flags & CE2Material::EffectFlag_UseRGBFalloff) );
			if ( sp->flags & ( CE2Material::EffectFlag_UseFalloff | CE2Material::EffectFlag_UseRGBFalloff ) ) {
				setValue<float>( q, "Falloff Start Angle", sp->falloffStartAngle );
				setValue<float>( q, "Falloff Stop Angle", sp->falloffStopAngle );
				setValue<float>( q, "Falloff Start Opacity", sp->falloffStartOpacity );
				setValue<float>( q, "Falloff Stop Opacity", sp->falloffStopOpacity );
			}
			break;
		}
		setValue<bool>( o, "Vertex Color Blend", bool(sp->flags & CE2Material::EffectFlag_VertexColorBlend) );
		setValue<bool>( o, "Is Alpha Tested", bool(sp->flags & CE2Material::EffectFlag_IsAlphaTested) );
		if ( sp->flags & CE2Material::EffectFlag_IsAlphaTested )
			setValue<float>( o, "Alpha Test Threshold", sp->alphaThreshold );
		setValue<bool>( o, "No Half Res Optimization", bool(sp->flags & CE2Material::EffectFlag_NoHalfResOpt) );
		setValue<bool>( o, "Soft Effect", bool(sp->flags & CE2Material::EffectFlag_SoftEffect) );
		setValue<float>( o, "Soft Falloff Depth", sp->softFalloffDepth );
		setValue<bool>( o, "Emissive Only Effect", bool(sp->flags & CE2Material::EffectFlag_EmissiveOnly) );
		setValue<bool>( o, "Emissive Only Automatically Applied", bool(sp->flags & CE2Material::EffectFlag_EmissiveOnlyAuto) );
		setValue<bool>( o, "Receive Directional Shadows", bool(sp->flags & CE2Material::EffectFlag_DirShadows) );
		setValue<bool>( o, "Receive Non-Directional Shadows", bool(sp->flags & CE2Material::EffectFlag_NonDirShadows) );
		setValue<bool>( o, "Is Glass", bool(sp->flags & CE2Material::EffectFlag_IsGlass) );
		setValue<bool>( o, "Frosting", bool(sp->flags & CE2Material::EffectFlag_Frosting) );
		if ( sp->flags & CE2Material::EffectFlag_Frosting ) {
			setValue<float>( o, "Frosting Unblurred Background Alpha Blend", sp->frostingBgndBlend );
			setValue<float>( o, "Frosting Blur Bias", sp->frostingBlurBias );
		}
		setValue<float>( o, "Material Overall Alpha", sp->materialAlpha );
		setValue<bool>( o, "Z Test", bool(sp->flags & CE2Material::EffectFlag_ZTest) );
		setValue<bool>( o, "Z Write", bool(sp->flags & CE2Material::EffectFlag_ZWrite) );
		setValue<quint8>( o, "Blending Mode", sp->blendMode );
		setValue<bool>( o, "Backlighting Enable", bool(sp->flags & CE2Material::EffectFlag_BacklightEnable) );
		if ( sp->flags & CE2Material::EffectFlag_BacklightEnable ) {
			setValue<float>( o, "Backlighting Scale", sp->backlightScale );
			setValue<float>( o, "Backlighting Sharpness", sp->backlightSharpness );
			setValue<float>( o, "Backlighting Transparency Factor", sp->backlightTransparency );
			setValue<Color4>( o, "Backlighting Tint Color", Color4( sp->backlightTintColor[0], sp->backlightTintColor[1], sp->backlightTintColor[2], sp->backlightTintColor[3] ) );
		}
		setValue<bool>( o, "Depth MV Fixup", bool(sp->flags & CE2Material::EffectFlag_MVFixup) );
		setValue<bool>( o, "Depth MV Fixup Edges Only", bool(sp->flags & CE2Material::EffectFlag_MVFixupEdgesOnly) );
		setValue<bool>( o, "Force Render Before OIT", bool(sp->flags & CE2Material::EffectFlag_RenderBeforeOIT) );
		setValue<bool>( o, "Force Render Before Clouds", bool(sp->flags & CE2Material::EffectFlag_RenderBeforeClouds) );
		setValue<quint16>( o, "Depth Bias In Ulp", quint16(sp->depthBias) );
	}
	bool	layeredEdgeFalloff = false;
	if ( isEffect ) {
		layeredEdgeFalloff = ( material && material->layeredEdgeFalloff );
		setValue<bool>( m, "Use Layered Edge Falloff", layeredEdgeFalloff );
	}
	if ( layeredEdgeFalloff && ( o = getItem( m, "Layered Edge Falloff" ) ) != nullptr ) {
		const CE2Material::LayeredEdgeFalloff *	sp = material->layeredEdgeFalloff;
		setValue<Vector3>( o, "Falloff Start Angles", Vector3( sp->falloffStartAngles[0], sp->falloffStartAngles[1], sp->falloffStartAngles[2] ) );
		setValue<Vector3>( o, "Falloff Stop Angles", Vector3( sp->falloffStopAngles[0], sp->falloffStopAngles[1], sp->falloffStopAngles[2] ) );
		setValue<Vector3>( o, "Falloff Start Opacities", Vector3( sp->falloffStartOpacities[0], sp->falloffStartOpacities[1], sp->falloffStartOpacities[2] ) );
		setValue<Vector3>( o, "Falloff Stop Opacities", Vector3( sp->falloffStopOpacities[0], sp->falloffStopOpacities[1], sp->falloffStopOpacities[2] ) );
		setValue<quint8>( o, "Active Layers Mask", sp->activeLayersMask );
		setValue<bool>( o, "Use RGB Falloff", sp->useRGBFalloff );
	}
	bool	isDecal = false;
	if ( material )
		isDecal = bool( material->flags & CE2Material::Flag_IsDecal );
	setValue<bool>( m, "Is Decal", isDecal );
	if ( isDecal && ( o = getItem( m, "Decal Settings" ) ) != nullptr ) {
		const CE2Material::DecalSettings *	sp = material->decalSettings;
		setValue<float>( o, "Material Overall Alpha", sp->decalAlpha );
		setValue<quint32>( o, "Write Mask", sp->writeMask );
		setValue<bool>( o, "Is Planet", sp->isPlanet );
		setValue<bool>( o, "Is Projected", sp->isProjected );
		if ( sp->isProjected ) {
			setValue<bool>( o, "Use Parallax Occlusion Mapping", sp->useParallaxMapping );
			setValue<QString>( o, "Surface Height Map", sp->surfaceHeightMap->data() );
			setValue<float>( o, "Parallax Occlusion Scale", sp->parallaxOcclusionScale );
			setValue<bool>( o, "Parallax Occlusion Shadows", sp->parallaxOcclusionShadows );
			setValue<quint8>( o, "Max Parallax Occlusion Steps", sp->maxParallaxSteps );
			setValue<quint8>( o, "Render Layer", sp->renderLayer );
			setValue<bool>( o, "Use G Buffer Normals", sp->useGBufferNormals );
		}
		setValue<quint8>( o, "Blend Mode", sp->blendMode );
		setValue<bool>( o, "Animated Decal Ignores TAA", sp->animatedDecalIgnoresTAA );
	}
	bool	isWater = false;
	if ( material )
		isWater = bool( material->flags & CE2Material::Flag_IsWater );
	setValue<bool>( m, "Is Water", isWater );
	if ( isWater && ( o = getItem( m, "Water Settings" ) ) != nullptr ) {
		const CE2Material::WaterSettings *	sp = material->waterSettings;
		setValue<float>( o, "Water Edge Falloff", sp->waterEdgeFalloff );
		setValue<float>( o, "Water Wetness Max Depth", sp->waterWetnessMaxDepth );
		setValue<float>( o, "Water Edge Normal Falloff", sp->waterEdgeNormalFalloff );
		setValue<float>( o, "Water Depth Blur", sp->waterDepthBlur );
		setValue<float>( o, "Water Refraction Magnitude", sp->reflectance[3] );
		setValue<float>( o, "Phytoplankton Reflectance Color R", sp->phytoplanktonReflectance[0] );
		setValue<float>( o, "Phytoplankton Reflectance Color G", sp->phytoplanktonReflectance[1] );
		setValue<float>( o, "Phytoplankton Reflectance Color B", sp->phytoplanktonReflectance[2] );
		setValue<float>( o, "Sediment Reflectance Color R", sp->sedimentReflectance[0] );
		setValue<float>( o, "Sediment Reflectance Color G", sp->sedimentReflectance[1] );
		setValue<float>( o, "Sediment Reflectance Color B", sp->sedimentReflectance[2] );
		setValue<float>( o, "Yellow Matter Reflectance Color R", sp->yellowMatterReflectance[0] );
		setValue<float>( o, "Yellow Matter Reflectance Color G", sp->yellowMatterReflectance[1] );
		setValue<float>( o, "Yellow Matter Reflectance Color B", sp->yellowMatterReflectance[2] );
		setValue<float>( o, "Max Concentration Plankton", sp->phytoplanktonReflectance[3] );
		setValue<float>( o, "Max Concentration Sediment", sp->sedimentReflectance[3] );
		setValue<float>( o, "Max Concentration Yellow Matter", sp->yellowMatterReflectance[3] );
		setValue<float>( o, "Reflectance R", sp->reflectance[0] );
		setValue<float>( o, "Reflectance G", sp->reflectance[1] );
		setValue<float>( o, "Reflectance B", sp->reflectance[2] );
		setValue<bool>( o, "Low LOD", sp->lowLOD );
		setValue<bool>( o, "Placed Water", sp->placedWater );
	}
	bool	isEmissive = false;
	if ( material )
		isEmissive = bool( material->flags & CE2Material::Flag_Emissive );
	setValue<bool>( m, "Is Emissive", isEmissive );
	if ( isEmissive && ( o = getItem( m, "Emissive Settings" ) ) != nullptr ) {
		const CE2Material::EmissiveSettings *	sp = material->emissiveSettings;
		setValue<quint8>( o, "Emissive Source Layer", sp->sourceLayer );
		setValue<Color4>( o, "Emissive Tint", Color4( sp->emissiveTint[0], sp->emissiveTint[1], sp->emissiveTint[2], sp->emissiveTint[3] ) );
		setValue<quint8>( o, "Emissive Mask Source Blender", sp->maskSourceBlender );
		setValue<float>( o, "Emissive Clip Threshold", sp->clipThreshold );
		setValue<bool>( o, "Adaptive Emittance", sp->adaptiveEmittance );
		setValue<float>( o, "Luminous Emittance", sp->luminousEmittance );
		setValue<float>( o, "Exposure Offset", sp->exposureOffset );
		setValue<bool>( o, "Enable Adaptive Limits", sp->enableAdaptiveLimits );
		setValue<float>( o, "Max Offset Emittance", sp->maxOffset );
		setValue<float>( o, "Min Offset Emittance", sp->minOffset );
	}
	bool	layeredEmissivity = false;
	if ( material )
		layeredEmissivity = bool( material->flags & CE2Material::Flag_LayeredEmissivity );
	setValue<bool>( m, "Layered Emissivity", layeredEmissivity );
	if ( layeredEmissivity && ( o = getItem( m, "Layered Emissivity Settings" ) ) != nullptr ) {
		const CE2Material::LayeredEmissiveSettings *	sp = material->layeredEmissiveSettings;
		setValue<quint8>( o, "First Layer Index", sp->layer1Index );
		setValue<Color4>( o, "First Layer Tint", ByteColor4( sp->layer1Tint ) );
		setValue<quint8>( o, "First Layer Mask Source", sp->layer1MaskIndex );
		setValue<bool>( o, "Second Layer Active", sp->layer2Active );
		if ( sp->layer2Active ) {
			setValue<quint8>( o, "Second Layer Index", sp->layer2Index );
			setValue<Color4>( o, "Second Layer Tint", ByteColor4( sp->layer2Tint ) );
			setValue<quint8>( o, "Second Layer Mask Source", sp->layer2MaskIndex );
			setValue<quint8>( o, "First Blender Index", sp->blender1Index );
			setValue<quint8>( o, "First Blender Mode", sp->blender1Mode );
		}
		setValue<bool>( o, "Third Layer Active", sp->layer3Active );
		if ( sp->layer3Active ) {
			setValue<quint8>( o, "Third Layer Index", sp->layer3Index );
			setValue<Color4>( o, "Third Layer Tint", ByteColor4( sp->layer3Tint ) );
			setValue<quint8>( o, "Third Layer Mask Source", sp->layer3MaskIndex );
			setValue<quint8>( o, "Second Blender Index", sp->blender2Index );
			setValue<quint8>( o, "Second Blender Mode", sp->blender2Mode );
		}
		setValue<float>( o, "Emissive Clip Threshold", sp->clipThreshold );
		setValue<bool>( o, "Adaptive Emittance", sp->adaptiveEmittance );
		setValue<float>( o, "Luminous Emittance", sp->luminousEmittance );
		setValue<float>( o, "Exposure Offset", sp->exposureOffset );
		setValue<bool>( o, "Enable Adaptive Limits", sp->enableAdaptiveLimits );
		setValue<float>( o, "Max Offset Emittance", sp->maxOffset );
		setValue<float>( o, "Min Offset Emittance", sp->minOffset );
		setValue<bool>( o, "Ignores Fog", sp->ignoresFog );
	}
	bool	isTranslucent = false;
	if ( material )
		isTranslucent = bool( material->flags & CE2Material::Flag_Translucency );
	setValue<bool>( m, "Is Translucent", isTranslucent );
	if ( isTranslucent && ( o = getItem( m, "Translucency Settings" ) ) != nullptr ) {
		const CE2Material::TranslucencySettings *	sp = material->translucencySettings;
		setValue<bool>( o, "Is Thin", sp->isThin );
		setValue<bool>( o, "Flip Back Face Normals In View Space", sp->flipBackFaceNormalsInVS );
		setValue<bool>( o, "Use Subsurface Scattering", sp->useSSS );
		if ( sp->useSSS ) {
			setValue<float>( o, "Subsurface Scattering Width", sp->sssWidth );
			setValue<float>( o, "Subsurface Scattering Strength", sp->sssStrength );
		}
		setValue<float>( o, "Transmissive Scale", sp->transmissiveScale );
		setValue<float>( o, "Transmittance Width", sp->transmittanceWidth );
		setValue<float>( o, "Spec Lobe 0 Roughness Scale", sp->specLobe0RoughnessScale );
		setValue<float>( o, "Spec Lobe 1 Roughness Scale", sp->specLobe1RoughnessScale );
		setValue<quint8>( o, "Transmittance Source Layer", sp->sourceLayer );
	}
}

void NifModel::loadFO76Material( const QModelIndex & parent, const void * material )
{
	NifItem *	p = getItem( parent, false );
	if ( p && !p->hasName( "Material" ) )
		p = getItem( p, "Material" );
	if ( !p || !material )
		return;
	for ( auto c : p->childIter() )
		c->invalidateCondition();

	const Material &	mat = *( static_cast< const Material * >( material ) );
	const ShaderMaterial *	bgsm = nullptr;
	const EffectMaterial *	bgem = nullptr;
	if ( typeid( mat ) == typeid( ShaderMaterial ) )
		bgsm = static_cast< const ShaderMaterial * >( material );
	if ( typeid( mat ) == typeid( EffectMaterial ) )
		bgem = static_cast< const EffectMaterial * >( material );

	setValue<quint32>( p, "Version", mat.version );
	quint16	shaderFlags1 = quint16( mat.bTileU ) | ( quint16(mat.bTileV) << 1 );
	shaderFlags1 |= ( quint16(bool(mat.bAlphaBlend)) << 2 ) | ( quint16(bool(mat.bAlphaTest)) << 3 );
	shaderFlags1 |= ( quint16(bool(mat.bZBufferWrite)) << 4 ) | ( quint16(bool(mat.bZBufferTest)) << 5 );
	shaderFlags1 |= ( quint16(bool(mat.bScreenSpaceReflections)) << 6 ) | ( quint16(bool(mat.bWetnessControl_ScreenSpaceReflections)) << 7 );
	shaderFlags1 |= ( quint16(bool(mat.bDecal)) << 8 ) | ( quint16(bool(mat.bTwoSided)) << 9 );
	shaderFlags1 |= ( quint16(bool(mat.bDecalNoFade)) << 10 ) | ( quint16(bool(mat.bNonOccluder)) << 11 );
	shaderFlags1 |= ( quint16(bool(mat.bRefraction)) << 12 ) | ( quint16(bool(mat.bRefractionFalloff)) << 13 );
	shaderFlags1 |= ( quint16(bool(mat.bEnvironmentMapping)) << 14 ) | ( quint16(bool(mat.bGrayscaleToPaletteColor)) << 15 );
	setValue<quint16>( p, "Shader Flags 1", shaderFlags1 );

	quint32	shaderFlags2 = 0;
	quint16	shaderFlags3 = 0;
	qsizetype	textureCnt = 0;
	if ( bgsm ) {
		shaderFlags2 = quint32( bool(bgsm->bEnableEditorAlphaRef) ) | ( quint32(bool(bgsm->bTranslucency)) << 1 );
		shaderFlags2 |= ( quint32(bool(bgsm->bTranslucencyThickObject)) << 2 ) | ( quint32(bool(bgsm->bTranslucencyMixAlbedoWithSubsurfaceCol)) << 3 );
		shaderFlags2 |= ( quint32(bool(bgsm->bSpecularEnabled)) << 4 ) | ( quint32(bool(bgsm->bPBR)) << 5 );
		shaderFlags2 |= ( quint32(bool(bgsm->bCustomPorosity)) << 6 ) | ( quint32(bool(bgsm->bAnisoLighting)) << 7 );
		shaderFlags2 |= ( quint32(bool(bgsm->bEmitEnabled)) << 8 ) | ( quint32(bool(bgsm->bModelSpaceNormals)) << 9 );
		shaderFlags2 |= ( quint32(bool(bgsm->bExternalEmittance)) << 10 ) | ( quint32(bool(bgsm->bUseAdaptativeEmissive)) << 11 );
		shaderFlags2 |= ( quint32(bool(bgsm->bReceiveShadows)) << 12 ) | ( quint32(bool(bgsm->bHideSecret)) << 13 );
		shaderFlags2 |= ( quint32(bool(bgsm->bCastShadows)) << 14 ) | ( quint32(bool(bgsm->bDissolveFade)) << 15 );
		shaderFlags2 |= ( quint32(bool(bgsm->bAssumeShadowmask)) << 16 ) | ( quint32(bool(bgsm->bGlowmap)) << 17 );
		shaderFlags2 |= ( quint32(bool(bgsm->bHair)) << 18 ) | ( quint32(bool(bgsm->bTree)) << 19 );
		shaderFlags2 |= ( quint32(bool(bgsm->bFacegen)) << 20 ) | ( quint32(bool(bgsm->bSkinTint)) << 21 );
		shaderFlags2 |= ( quint32(bool(bgsm->bTessellate)) << 22 ) | ( quint32(bool(bgsm->bSkewSpecularAlpha)) << 23 );
		shaderFlags2 |= ( quint32(bool(bgsm->bTerrain)) << 24 );
		setValue<quint32>( p, "Shader Flags 2", shaderFlags2 );
		textureCnt = 10;
	}
	if ( bgem ) {
		shaderFlags3 = quint16( bool(bgem->bEnvironmentMapping) ) | ( quint16(bool(bgem->bBloodEnabled)) << 1 );
		shaderFlags3 |= ( quint16(bool(bgem->bEffectLightingEnabled)) << 2 ) | ( quint16(bool(bgem->bFalloffEnabled)) << 3 );
		shaderFlags3 |= ( quint16(bool(bgem->bFalloffColorEnabled)) << 4 ) | ( quint16(bool(bgem->bGrayscaleToPaletteAlpha)) << 5 );
		shaderFlags3 |= ( quint16(bool(bgem->bSoftEnabled)) << 6 ) | ( quint16(bool(bgem->bGlowmap)) << 7 );
		shaderFlags3 |= ( quint16(bool(bgem->bEffectPbrSpecular)) << 8 );
		shaderFlags3 |= ( quint16(bool(bgem->bGlassEnabled)) << 9 );
		setValue<quint16>( p, "Shader Flags 2", shaderFlags3 );
		textureCnt = ( bgem->version < 21 ? 8 : 10 );
	}

	// common material properties
	setValue<Vector2>( p, "UV Offset", Vector2( mat.fUOffset, mat.fVOffset ) );
	setValue<Vector2>( p, "UV Scale", Vector2( mat.fUScale, mat.fVScale ) );
	setValue<float>( p, "Alpha", mat.fAlpha );
	setValue<quint16>( p, "Alpha Source Blend Mode", quint16(mat.iAlphaSrc & 0x0F) );
	setValue<quint16>( p, "Alpha Destination Blend Mode", quint16(mat.iAlphaDst & 0x0F) );
	setValue<quint8>( p, "Alpha Test Threshold", mat.iAlphaTestRef );
	setValue<float>( p, "Refraction Power", mat.fRefractionPower );
	setValue<quint8>( p, "Write Mask", mat.ucMaskWrites );

	// texture set
	for ( qsizetype i = 0; i < textureCnt; i++ ) {
		if ( mat.textureList.size() > i )
			setValue<QString>( p, QString("Texture %1").arg(i), mat.textureList[i] );
		else
			setValue<QString>( p, QString("Texture %1").arg(i), "" );
	}

	// shader material properties
	if ( bgsm ) {
		setValue<Color3>( p, "Translucency Subsurface Color", bgsm->cTranslucencySubsurfaceColor );
		setValue<float>( p, "Translucency Transmissive Scale", bgsm->fTranslucencyTransmissiveScale );
		setValue<float>( p, "Translucency Turbulence", bgsm->fTranslucencyTurbulence );
		setValue<Color3>( p, "Specular Color", bgsm->cSpecularColor );
		setValue<float>( p, "Specular Strength", bgsm->fSpecularMult );
		setValue<float>( p, "Smoothness", bgsm->fSmoothness );
		setValue<float>( p, "Fresnel Power", bgsm->fFresnelPower );
		NifItem *	o = getItem( p, "Wetness" );
		if ( o ) {
			setValue<float>( o, "Spec Scale", bgsm->fWetnessControl_SpecScale );
			setValue<float>( o, "Spec Power", bgsm->fWetnessControl_SpecPowerScale );
			setValue<float>( o, "Min Var", bgsm->fWetnessControl_SpecMinvar );
			setValue<float>( o, "Fresnel Power", bgsm->fWetnessControl_FresnelPower );
			setValue<float>( o, "Metalness", bgsm->fWetnessControl_Metalness );
		}
		setValue<float>( p, "Porosity Value", bgsm->fPorosityValue );
		setValue<QString>( p, "Root Material", bgsm->sRootMaterialPath );
		if ( shaderFlags2 & 0x00000100 )
			setValue<Color3>( p, "Emissive Color", bgsm->cEmittanceColor );
		setValue<float>( p, "Emissive Multiple", bgsm->fEmittanceMult );
		o = getItem( p, "Luminance" );
		if ( o ) {
			setValue<float>( o, "Lum Emittance", mat.fLumEmittance );
			setValue<float>( o, "Exposure Offset", mat.fAdaptativeEmissive_ExposureOffset );
			setValue<float>( o, "Final Exposure Min", mat.fAdaptativeEmissive_FinalExposureMin );
			setValue<float>( o, "Final Exposure Max", mat.fAdaptativeEmissive_FinalExposureMax );
		}
		setValue<Color3>( p, "Hair Tint Color", bgsm->cHairTintColor );
		setValue<float>( p, "Grayscale to Palette Scale", bgsm->fGrayscaleToPaletteScale );
		if ( shaderFlags2 & 0x01000000 ) {
			setValue<float>( p, "Terrain Threshold Falloff", bgsm->fTerrainThresholdFalloff );
			setValue<float>( p, "Terrain Tiling Distance", bgsm->fTerrainTilingDistance );
			setValue<float>( p, "Terrain Rotation Angle", bgsm->fTerrainRotationAngle );
		}
	}

	// effect material properties
	if ( bgem ) {
		if ( bgem->bGlassEnabled ) {
			setValue<Color3>( p, "Glass Fresnel Color", bgem->cGlassFresnelColor );
			setValue<float>( p, "Glass Refraction Scale", bgem->fGlassRefractionScaleBase );
			setValue<float>( p, "Glass Blur Scale", bgem->fGlassBlurScaleBase );
		}
		setValue<float>( p, "Environment Map Scale", bgem->fEnvironmentMappingMaskScale );
		setValue<Color3>( p, "Base Color", bgem->cBaseColor );
		setValue<float>( p, "Base Color Scale", bgem->fBaseColorScale );
		setValue<float>( p, "Falloff Start Angle", bgem->fFalloffStartAngle );
		setValue<float>( p, "Falloff Stop Angle", bgem->fFalloffStopAngle );
		setValue<float>( p, "Falloff Start Opacity", bgem->fFalloffStartOpacity );
		setValue<float>( p, "Falloff Stop Opacity", bgem->fFalloffStopOpacity );
		setValue<float>( p, "Lighting Influence", bgem->fLightingInfluence );
		setValue<quint8>( p, "Env Map Min LOD", bgem->iEnvmapMinLOD );
		setValue<float>( p, "Soft Falloff Depth", bgem->fSoftDepth );
		setValue<Color3>( p, "Emittance Color", bgem->cEmittanceColor );
		setValue<float>( p, "Adaptive Emissive Exposure Offset", bgem->fAdaptativeEmissive_ExposureOffset );
		setValue<float>( p, "Adaptive Emissive Exposure Min", bgem->fAdaptativeEmissive_FinalExposureMin );
		setValue<float>( p, "Adaptive Emissive Exposure Max", bgem->fAdaptativeEmissive_FinalExposureMax );
	}
}

const std::string_view * NifModel::copySFMatString( AllocBuffers & bufs, const QString & s )
{
	static const std::string_view	emptyString( "", 0 );
	if ( s.isEmpty() )
		return &emptyString;
	size_t	len = 0;
	for ( const QChar & c : s ) {
		std::uint16_t	tmp = std::uint16_t( c.unicode() );
		len = len + ( tmp < 0x0080U ? 1 : ( tmp < 0x0800U ? 2 : 3 ) );
	}
	size_t	allocBytes = len + sizeof( std::string_view ) + 1;
	std::string_view *	storedString =
		reinterpret_cast< std::string_view * >( bufs.allocateSpace( allocBytes, alignof( std::string_view ) ) );
	char *	data = reinterpret_cast< char * >( storedString ) + sizeof( std::string_view );
	(void) new( storedString ) std::string_view( data, len );
	for ( const QChar & c : s ) {
		std::uint16_t	tmp = std::uint16_t( c.unicode() );
		if ( tmp < 0x0080U ) [[likely]] {
			data[0] = char( tmp );
			data++;
		} else if ( tmp < 0x0800U ) {
			data[0] = char( ( tmp >> 6 ) | 0xC0 );
			data[1] = char( ( tmp & 0x3F ) | 0x80 );
			data = data + 2;
		} else {
			data[0] = char( ( tmp >> 12 ) | 0xE0 );
			data[1] = char( ( ( tmp >> 6 ) & 0x3F ) | 0x80 );
			data[2] = char( ( tmp & 0x3F ) | 0x80 );
			data = data + 3;
		}
	}
	return storedString;
}

const void * NifModel::createSFBlender( AllocBuffers & bufs, const NifItem * parent ) const
{
	CE2Material::Blender *	blender = bufs.constructObject< CE2Material::Blender >();
	if ( !parent ) {
		blender->uvStream = reinterpret_cast< const CE2Material::UVStream * >( createSFUVStream( bufs, nullptr ) );
	} else {
		blender->name = copySFMatString( bufs, get<QString>( parent, "Name" ) )->data();
		blender->uvStream = reinterpret_cast< const CE2Material::UVStream * >(
								createSFUVStream( bufs, getItem( parent, "UV Stream" ) ) );
		blender->textureReplacementEnabled =
			createSFTextureWithReplacement( blender->texturePath, blender->textureReplacement,
											bufs, getItem( parent, "Mask Texture" ) );
		blender->blendMode = std::min< quint8 >( get<quint8>( parent, "Blend Mode" ), 5 );
		blender->colorChannel = get<quint8>( parent, "Vertex Color Channel" ) & 3;
		blender->floatParams[0] = get<float>( parent, "Height Blend Threshold" );
		blender->floatParams[1] = get<float>( parent, "Height Blend Factor" );
		blender->floatParams[2] = get<float>( parent, "Position" );
		blender->floatParams[3] = 1.0f - get<float>( parent, "Contrast" );
		blender->floatParams[4] = get<float>( parent, "Mask Intensity" );
		blender->boolParams[0] = get<bool>( parent, "Blend Color" );
		blender->boolParams[1] = get<bool>( parent, "Blend Metalness" );
		blender->boolParams[2] = get<bool>( parent, "Blend Roughness" );
		blender->boolParams[3] = get<bool>( parent, "Blend Normals" );
		blender->boolParams[4] = get<bool>( parent, "Blend Normals Additively" );
		blender->boolParams[5] = get<bool>( parent, "Use Vertex Color" );
		blender->boolParams[6] = get<bool>( parent, "Blend Ambient Occlusion" );
		blender->boolParams[7] = get<bool>( parent, "Use Detail Blend Mask" );
	}
	if ( blender->uvStream )
		const_cast< CE2Material::UVStream * >( blender->uvStream )->parent = blender;
	return blender;
}

const void * NifModel::createSFLayer( AllocBuffers & bufs, const NifItem * parent ) const
{
	CE2Material::Layer *	layer = bufs.constructObject< CE2Material::Layer >();
	if ( !parent ) {
		layer->material = reinterpret_cast< const CE2Material::Material * >( createSFMaterial( bufs, nullptr ) );
		layer->uvStream = reinterpret_cast< const CE2Material::UVStream * >( createSFUVStream( bufs, nullptr ) );
	} else {
		layer->name = copySFMatString( bufs, get<QString>( parent, "Name" ) )->data();
		layer->material = reinterpret_cast< const CE2Material::Material * >(
								createSFMaterial( bufs, getItem( parent, "Material" ) ) );
		layer->uvStream = reinterpret_cast< const CE2Material::UVStream * >(
								createSFUVStream( bufs, getItem( parent, "UV Stream" ) ) );
	}
	if ( layer->material )
		const_cast< CE2Material::Material * >( layer->material )->parent = layer;
	if ( layer->uvStream )
		const_cast< CE2Material::UVStream * >( layer->uvStream )->parent = layer;
	return layer;
}

const void * NifModel::createSFMaterial( AllocBuffers & bufs, const NifItem * parent ) const
{
	CE2Material::Material *	material = bufs.constructObject< CE2Material::Material >();
	if ( !parent ) {
		material->textureSet =
			reinterpret_cast< const CE2Material::TextureSet * >( createSFTextureSet( bufs, nullptr ) );
	} else {
		material->name = copySFMatString( bufs, get<QString>( parent, "Name" ) )->data();
		material->color = FloatVector4( get<Color4>( parent, "Color" ) );
		material->colorModeFlags = get<quint8>( parent, "Color Override Mode" ) & 3;
		material->flipbookFlags = (unsigned char) get<bool>( parent, "Is Flipbook" );
		if ( material->flipbookFlags ) {
			material->flipbookColumns = std::min< quint8 >( get<quint8>( parent, "Flipbook Columns" ), 127 );
			material->flipbookRows = std::min< quint8 >( get<quint8>( parent, "Flipbook Rows" ), 127 );
			material->flipbookFPS = std::min( std::max< float >( get<float>( parent, "Flipbook FPS" ), 0.1f ), 240.0f );
			if ( get<bool>( parent, "Flipbook Loops" ) )
				material->flipbookFlags = material->flipbookFlags | 2;
		}
		material->textureSet = reinterpret_cast< const CE2Material::TextureSet * >(
									createSFTextureSet( bufs, getItem( parent, "Texture Set" ) ) );
	}
	if ( material->textureSet )
		const_cast< CE2Material::TextureSet * >( material->textureSet )->parent = material;
	return material;
}

bool NifModel::createSFTextureWithReplacement( const std::string_view* & texturePath, std::uint32_t & replacementColor,
												AllocBuffers & bufs, const NifItem * parent ) const
{
	if ( !parent ) {
		texturePath = copySFMatString( bufs, QString() );
		return false;
	}
	texturePath = copySFMatString( bufs, get<QString>( parent, "Path" ) );
	if ( get<bool>( parent, "Replacement Enabled" ) ) {
		replacementColor = std::uint32_t( get<ByteColor4>( parent, "Replacement Color" ) );
		return true;
	}
	return false;
}

const void * NifModel::createSFTextureSet( AllocBuffers & bufs, const NifItem * parent ) const
{
	CE2Material::TextureSet *	textureSet = bufs.constructObject< CE2Material::TextureSet >();
	if ( parent ) {
		textureSet->name = copySFMatString( bufs, get<QString>( parent, "Name" ) )->data();
		textureSet->floatParam = get<float>( parent, "Normal Intensity" );
		std::uint32_t	textureEnableMask = get<quint32>( parent, "Enable Mask" );
		for ( int i = 0; i < CE2Material::TextureSet::maxTexturePaths; i++ ) {
			std::uint32_t	m = 1U << i;
			if ( !( textureEnableMask & m ) ) {
				textureSet->texturePaths[i] = copySFMatString( bufs, QString() );
				continue;
			}
			if ( createSFTextureWithReplacement( textureSet->texturePaths[i], textureSet->textureReplacements[i],
												bufs, getItem( parent, QString( "Texture %1" ).arg( i ) ) ) ) {
				textureSet->textureReplacementMask |= m;
			}
			if ( !textureSet->texturePaths[i]->empty() )
				textureSet->texturePathMask |= m;
		}
		textureSet->resolutionHint = get<quint8>( parent, "Resolution Hint" ) & 3;
		textureSet->disableMipBiasHint = get<bool>( parent, "Disable Mip Bias Hint" );
	}
	return textureSet;
}

const void * NifModel::createSFUVStream( AllocBuffers & bufs, const NifItem * parent ) const
{
	CE2Material::UVStream *	uvStream = bufs.constructObject< CE2Material::UVStream >();
	if ( parent ) {
		uvStream->name = copySFMatString( bufs, get<QString>( parent, "Name" ) )->data();
		uvStream->scaleAndOffset[0] = get<float>( parent, "U Scale" );
		uvStream->scaleAndOffset[1] = get<float>( parent, "V Scale" );
		uvStream->scaleAndOffset[2] = get<float>( parent, "U Offset" );
		uvStream->scaleAndOffset[3] = get<float>( parent, "V Offset" );
		uvStream->textureAddressMode = get<quint8>( parent, "Texture Address Mode" ) & 3;
		uvStream->channel = ( ( get<quint8>( parent, "Channel" ) - 1 ) & 1 ) + 1;
	}
	return uvStream;
}

const void * NifModel::updateSFMaterial( AllocBuffers & bufs, const QModelIndex & parent ) const
{
	if ( !parent.isValid() )
		return nullptr;
	const NifItem *	m = getItem( parent, false );
	if ( m && !m->hasName( "Material" ) )
		m = getItem( m, "Material" );
	if ( !m )
		return nullptr;

	CE2Material *	mat = bufs.constructObject< CE2Material >();
	mat->name = copySFMatString( bufs, get<QString>( m, "Name" ) )->data();

	for ( int l = 0; l < CE2Material::maxLayers; l++ ) {
		if ( !get<bool>( m, QString( "Layer %1 Enabled" ).arg( l ) ) )
			continue;

		mat->layers[l] = reinterpret_cast< const CE2Material::Layer * >(
							createSFLayer( bufs, getItem( m, QString( "Layer %1" ).arg( l ) ) ) );
		if ( mat->layers[l] )
			const_cast< CE2Material::Layer * >( mat->layers[l] )->parent = mat;
		if ( l > 0 ) {
			mat->blenders[l - 1] = reinterpret_cast< const CE2Material::Blender * >(
										createSFBlender( bufs, getItem( m, QString( "Blender %1" ).arg( l - 1 ) ) ) );
			if ( mat->blenders[l - 1] )
				const_cast< CE2Material::Blender * >( mat->blenders[l - 1] )->parent = mat;
		}
		mat->layerMask |= std::uint32_t( 1 << l );
	}

	QString	shaderModel = get<QString>( m, "Shader Model" ).trimmed();
	{
		size_t	n0 = 0;
		size_t	n2 = sizeof( CE2Material::shaderModelNames ) / sizeof( char * );
		while ( n2 > n0 ) {
			size_t	n1 = ( n0 + n2 ) >> 1;
			int	d = shaderModel.compare( QLatin1StringView( CE2Material::shaderModelNames[n1] ), Qt::CaseInsensitive );
			if ( d && n1 > n0 ) [[likely]] {
				if ( d < 0 )
					n2 = n1;
				else
					n0 = n1;
				continue;
			}
			if ( !d )
				mat->shaderModel = (unsigned char) n1;
			break;
		}
	}
	mat->shaderRoute = std::min< quint8 >( get<quint8>( m, "Shader Route" ), 5 );
	mat->setFlags( CE2Material::Flag_TwoSided, get<bool>( m, "Two Sided" ) );
	mat->physicsMaterialType = std::min< quint8 >( get<quint8>( m, "Physics Material Type" ), 7 );

	quint8	maxLayer = CE2Material::maxLayers - 1;
	quint8	maxBlender = CE2Material::maxBlenders - 1;
	const NifItem *	o;
	if ( mat->shaderRoute == 1 && get<bool>( m, "Has Opacity Component" )
		&& ( o = getItem( m, "Opacity Settings" ) ) != nullptr ) {
		mat->setFlags( CE2Material::Flag_HasOpacityComponent, true );
		mat->opacityLayer1 = std::min< quint8 >( get<quint8>( o, "First Layer Index" ), maxLayer );
		mat->setFlags( CE2Material::Flag_OpacityLayer2Active, get<bool>( o, "Second Layer Active" ) );
		mat->opacityLayer2 = std::min< quint8 >( get<quint8>( o, "Second Layer Index" ), maxLayer );
		mat->opacityBlender1 = std::min< quint8 >( get<quint8>( o, "First Blender Index" ), maxBlender );
		// opacity blender modes: "Lerp", "Additive", "Subtractive", "Multiplicative"
		mat->opacityBlender1Mode = get<quint8>( o, "First Blender Mode" ) & 3;
		mat->setFlags( CE2Material::Flag_OpacityLayer3Active, get<bool>( o, "Third Layer Active" ) );
		mat->opacityLayer3 = std::min< quint8 >( get<quint8>( o, "Third Layer Index" ), maxLayer );
		mat->opacityBlender2 = std::min< quint8 >( get<quint8>( o, "Second Blender Index" ), maxBlender );
		mat->opacityBlender2Mode = get<quint8>( o, "Second Blender Mode" ) & 3;
		mat->specularOpacityOverride = get<float>( o, "Specular Opacity Override" );
	}

	if ( mat->shaderRoute != 1 && get<bool>( m, "Has Opacity" ) && ( o = getItem( m, "Alpha Settings" ) ) != nullptr ) {
		mat->setFlags( CE2Material::Flag_HasOpacity, true );
		mat->alphaThreshold = get<float>( o, "Alpha Test Threshold" );
		mat->alphaSourceLayer = std::min< quint8 >( get<quint8>( o, "Opacity Source Layer" ), maxLayer );
		// alpha blender modes: "Linear", "Additive", "PositionContrast", "None"
		mat->alphaBlendMode = get<quint8>( o, "Alpha Blender Mode" ) & 3;
		mat->setFlags( CE2Material::Flag_AlphaDetailBlendMask, get<bool>( o, "Use Detail Blend Mask" ) );
		mat->setFlags( CE2Material::Flag_AlphaVertexColor, get<bool>( o, "Use Vertex Color" ) );
		if ( mat->flags & CE2Material::Flag_AlphaVertexColor )
			mat->alphaVertexColorChannel = get<quint8>( o, "Vertex Color Channel" ) & 3;
		mat->alphaUVStream = reinterpret_cast< const CE2Material::UVStream * >(
								createSFUVStream( bufs, getItem( o, "Opacity UV Stream" ) ) );
		if ( mat->alphaUVStream )
			const_cast< CE2Material::UVStream * >( mat->alphaUVStream )->parent = mat;
		mat->alphaHeightBlendThreshold = get<float>( o, "Height Blend Threshold" );
		mat->alphaHeightBlendFactor = get<float>( o, "Height Blend Factor" );
		mat->alphaPosition = get<float>( o, "Position" );
		mat->alphaContrast = get<float>( o, "Contrast" );
		mat->setFlags( CE2Material::Flag_DitheredTransparency, get<bool>( o, "Use Dithered Transparency" ) );
	}

	if ( get<bool>( m, "Detail Blend Mask Supported" ) && ( o = getItem( m, "Detail Blender Settings" ) ) != nullptr ) {
		CE2Material::DetailBlenderSettings *	sp = bufs.constructObject< CE2Material::DetailBlenderSettings >();
		sp->isEnabled = true;
		sp->textureReplacementEnabled = createSFTextureWithReplacement( sp->texturePath, sp->textureReplacement,
																		bufs, getItem( o, "Texture" ) );
		sp->uvStream = reinterpret_cast< const CE2Material::UVStream * >(
							createSFUVStream( bufs, getItem( o, "UV Stream" ) ) );
		if ( sp->uvStream )
			const_cast< CE2Material::UVStream * >( sp->uvStream )->parent = mat;
		mat->detailBlenderSettings = sp;
		mat->setFlags( CE2Material::Flag_UseDetailBlender, true );
	}

	if ( get<bool>( m, "Is Effect" ) && ( o = getItem( m, "Effect Settings" ) ) != nullptr ) {
		CE2Material::EffectSettings *	sp = bufs.constructObject< CE2Material::EffectSettings >();
		for ( const NifItem * q = getItem( o, "Falloff Settings (Deprecated)" ); q; ) {
			sp->setFlags( CE2Material::EffectFlag_UseFalloff, get<bool>( q, "Use Falloff" ) );
			sp->setFlags( CE2Material::EffectFlag_UseRGBFalloff, get<bool>( q, "Use RGB Falloff" ) );
			if ( sp->flags & ( CE2Material::EffectFlag_UseFalloff | CE2Material::EffectFlag_UseRGBFalloff ) ) {
				sp->falloffStartAngle = get<float>( q, "Falloff Start Angle" );
				sp->falloffStopAngle = get<float>( q, "Falloff Stop Angle" );
				sp->falloffStartOpacity = get<float>( q, "Falloff Start Opacity" );
				sp->falloffStopOpacity = get<float>( q, "Falloff Stop Opacity" );
			}
			break;
		}
		sp->setFlags( CE2Material::EffectFlag_VertexColorBlend, get<bool>( o, "Vertex Color Blend" ) );
		sp->setFlags( CE2Material::EffectFlag_IsAlphaTested, get<bool>( o, "Is Alpha Tested" ) );
		if ( sp->flags & CE2Material::EffectFlag_IsAlphaTested )
			sp->alphaThreshold = get<float>( o, "Alpha Test Threshold" );
		sp->setFlags( CE2Material::EffectFlag_NoHalfResOpt, get<bool>( o, "No Half Res Optimization" ) );
		sp->setFlags( CE2Material::EffectFlag_SoftEffect, get<bool>( o, "Soft Effect" ) );
		sp->softFalloffDepth = get<float>( o, "Soft Falloff Depth" );
		sp->setFlags( CE2Material::EffectFlag_EmissiveOnly, get<bool>( o, "Emissive Only Effect" ) );
		sp->setFlags( CE2Material::EffectFlag_EmissiveOnlyAuto, get<bool>( o, "Emissive Only Automatically Applied" ) );
		sp->setFlags( CE2Material::EffectFlag_DirShadows, get<bool>( o, "Receive Directional Shadows" ) );
		sp->setFlags( CE2Material::EffectFlag_NonDirShadows, get<bool>( o, "Receive Non-Directional Shadows" ) );
		sp->setFlags( CE2Material::EffectFlag_IsGlass, get<bool>( o, "Is Glass" ) );
		sp->setFlags( CE2Material::EffectFlag_Frosting, get<bool>( o, "Frosting" ) );
		if ( sp->flags & CE2Material::EffectFlag_Frosting ) {
			sp->frostingBgndBlend = get<float>( o, "Frosting Unblurred Background Alpha Blend" );
			sp->frostingBlurBias = get<float>( o, "Frosting Blur Bias" );
		}
		sp->materialAlpha = get<float>( o, "Material Overall Alpha" );
		sp->setFlags( CE2Material::EffectFlag_ZTest, get<bool>( o, "Z Test" ) );
		sp->setFlags( CE2Material::EffectFlag_ZWrite, get<bool>( o, "Z Write" ) );
		sp->blendMode = get<quint8>( o, "Blending Mode" ) & 7;
		sp->setFlags( CE2Material::EffectFlag_BacklightEnable, get<bool>( o, "Backlighting Enable" ) );
		if ( sp->flags & CE2Material::EffectFlag_BacklightEnable ) {
			sp->backlightScale = get<float>( o, "Backlighting Scale" );
			sp->backlightSharpness = get<float>( o, "Backlighting Sharpness" );
			sp->backlightTransparency = get<float>( o, "Backlighting Transparency Factor" );
			sp->backlightTintColor = FloatVector4( get<Color4>( o, "Backlighting Tint Color" ) );
		}
		sp->setFlags( CE2Material::EffectFlag_MVFixup, get<bool>( o, "Depth MV Fixup" ) );
		sp->setFlags( CE2Material::EffectFlag_MVFixupEdgesOnly, get<bool>( o, "Depth MV Fixup Edges Only" ) );
		sp->setFlags( CE2Material::EffectFlag_RenderBeforeOIT, get<bool>( o, "Force Render Before OIT" ) );
		sp->setFlags( CE2Material::EffectFlag_RenderBeforeClouds, get<bool>( o, "Force Render Before Clouds" ) );
		sp->depthBias = get<quint16>( o, "Depth Bias In Ulp" );
		mat->effectSettings = sp;
		mat->setFlags( CE2Material::Flag_IsEffect | CE2Material::Flag_AlphaBlending, true );
	}

	if ( ( mat->flags & CE2Material::Flag_IsEffect ) && get<bool>( m, "Use Layered Edge Falloff" )
		&& ( o = getItem( m, "Layered Edge Falloff" ) ) != nullptr ) {
		CE2Material::LayeredEdgeFalloff *	sp = bufs.constructObject< CE2Material::LayeredEdgeFalloff >();
		FloatVector4	tmp = FloatVector4( get<Vector3>( o, "Falloff Start Angles" ) );
		tmp.convertToVector3( &(sp->falloffStartAngles[0]) );
		tmp = FloatVector4( get<Vector3>( o, "Falloff Stop Angles" ) );
		tmp.convertToVector3( &(sp->falloffStopAngles[0]) );
		tmp = FloatVector4( get<Vector3>( o, "Falloff Start Opacities" ) );
		tmp.convertToVector3( &(sp->falloffStartOpacities[0]) );
		tmp = FloatVector4( get<Vector3>( o, "Falloff Stop Opacities" ) );
		tmp.convertToVector3( &(sp->falloffStopOpacities[0]) );
		sp->activeLayersMask = get<quint8>( o, "Active Layers Mask" ) & 7;
		sp->useRGBFalloff = get<bool>( o, "Use RGB Falloff" );
		mat->layeredEdgeFalloff = sp;
		mat->setFlags( CE2Material::Flag_LayeredEdgeFalloff, true );
	}

	if ( get<bool>( m, "Is Decal" ) && ( o = getItem( m, "Decal Settings" ) ) != nullptr ) {
		CE2Material::DecalSettings *	sp = bufs.constructObject< CE2Material::DecalSettings >();
		sp->isDecal = true;
		sp->decalAlpha = get<float>( o, "Material Overall Alpha" );
		sp->writeMask = get<quint32>( o, "Write Mask" );
		sp->isPlanet = get<bool>( o, "Is Planet" );
		sp->isProjected = get<bool>( o, "Is Projected" );
		if ( sp->isProjected ) {
			sp->useParallaxMapping = get<bool>( o, "Use Parallax Occlusion Mapping" );
			sp->surfaceHeightMap = copySFMatString( bufs, get<QString>( o, "Surface Height Map" ) );
			sp->parallaxOcclusionScale = get<float>( o, "Parallax Occlusion Scale" );
			sp->parallaxOcclusionShadows = get<bool>( o, "Parallax Occlusion Shadows" );
			sp->maxParallaxSteps = get<quint8>( o, "Max Parallax Occlusion Steps" );
			// "Top", "Middle"
			sp->renderLayer = get<quint8>( o, "Render Layer" ) & 1;
			sp->useGBufferNormals = get<bool>( o, "Use G Buffer Normals" );
		}
		// "None", "Additive"
		sp->blendMode = get<quint8>( o, "Blend Mode" ) & 1;
		sp->animatedDecalIgnoresTAA = get<bool>( o, "Animated Decal Ignores TAA" );
		mat->decalSettings = sp;
		mat->setFlags( CE2Material::Flag_IsDecal | CE2Material::Flag_AlphaBlending, true );
	}

	if ( get<bool>( m, "Is Water" ) && ( o = getItem( m, "Water Settings" ) ) != nullptr ) {
		CE2Material::WaterSettings *	sp = bufs.constructObject< CE2Material::WaterSettings >();
		sp->waterEdgeFalloff = get<float>( o, "Water Edge Falloff" );
		sp->waterWetnessMaxDepth = get<float>( o, "Water Wetness Max Depth" );
		sp->waterEdgeNormalFalloff = get<float>( o, "Water Edge Normal Falloff" );
		sp->waterDepthBlur = get<float>( o, "Water Depth Blur" );
		sp->reflectance[3] = get<float>( o, "Water Refraction Magnitude" );
		sp->phytoplanktonReflectance[0] = get<float>( o, "Phytoplankton Reflectance Color R" );
		sp->phytoplanktonReflectance[1] = get<float>( o, "Phytoplankton Reflectance Color G" );
		sp->phytoplanktonReflectance[2] = get<float>( o, "Phytoplankton Reflectance Color B" );
		sp->sedimentReflectance[0] = get<float>( o, "Sediment Reflectance Color R" );
		sp->sedimentReflectance[1] = get<float>( o, "Sediment Reflectance Color G" );
		sp->sedimentReflectance[2] = get<float>( o, "Sediment Reflectance Color B" );
		sp->yellowMatterReflectance[0] = get<float>( o, "Yellow Matter Reflectance Color R" );
		sp->yellowMatterReflectance[1] = get<float>( o, "Yellow Matter Reflectance Color G" );
		sp->yellowMatterReflectance[2] = get<float>( o, "Yellow Matter Reflectance Color B" );
		sp->phytoplanktonReflectance[3] = get<float>( o, "Max Concentration Plankton" );
		sp->sedimentReflectance[3] = get<float>( o, "Max Concentration Sediment" );
		sp->yellowMatterReflectance[3] = get<float>( o, "Max Concentration Yellow Matter" );
		sp->reflectance[0] = get<float>( o, "Reflectance R" );
		sp->reflectance[1] = get<float>( o, "Reflectance G" );
		sp->reflectance[2] = get<float>( o, "Reflectance B" );
		sp->lowLOD = get<bool>( o, "Low LOD" );
		sp->placedWater = get<bool>( o, "Placed Water" );
		mat->waterSettings = sp;
		mat->setFlags( CE2Material::Flag_IsWater | CE2Material::Flag_AlphaBlending, true );
	}

	if ( get<bool>( m, "Is Emissive" ) && ( o = getItem( m, "Emissive Settings" ) ) != nullptr ) {
		CE2Material::EmissiveSettings *	sp = bufs.constructObject< CE2Material::EmissiveSettings >();
		sp->isEnabled = true;
		sp->sourceLayer = std::min< quint8 >( get<quint8>( o, "Emissive Source Layer" ), maxLayer );
		sp->emissiveTint = FloatVector4( get<Color4>( o, "Emissive Tint" ) );
		// "None", "Blender 0", "Blender 1", "Blender 2"
		sp->maskSourceBlender = std::min< quint8 >( get<quint8>( o, "Emissive Mask Source Blender" ), 3 );
		sp->clipThreshold = get<float>( o, "Emissive Clip Threshold" );
		sp->adaptiveEmittance = get<bool>( o, "Adaptive Emittance" );
		sp->luminousEmittance = get<float>( o, "Luminous Emittance" );
		sp->exposureOffset = get<float>( o, "Exposure Offset" );
		sp->enableAdaptiveLimits = get<bool>( o, "Enable Adaptive Limits" );
		sp->maxOffset = get<float>( o, "Max Offset Emittance" );
		sp->minOffset = get<float>( o, "Min Offset Emittance" );
		mat->emissiveSettings = sp;
		mat->setFlags( CE2Material::Flag_Emissive, true );
	}

	if ( get<bool>( m, "Layered Emissivity" ) && ( o = getItem( m, "Layered Emissivity Settings" ) ) != nullptr ) {
		CE2Material::LayeredEmissiveSettings *	sp = bufs.constructObject< CE2Material::LayeredEmissiveSettings >();
		sp->isEnabled = true;
		sp->layer1Index = std::min< quint8 >( get<quint8>( o, "First Layer Index" ), maxLayer );
		sp->layer1Tint = std::uint32_t( get<ByteColor4>( o, "First Layer Tint" ) );
		// "None", "Blender 0", "Blender 1", "Blender 2"
		sp->layer1MaskIndex = std::min< quint8 >( get<quint8>( o, "First Layer Mask Source" ), 3 );
		sp->layer2Active = get<bool>( o, "Second Layer Active" );
		if ( sp->layer2Active ) {
			sp->layer2Index = std::min< quint8 >( get<quint8>( o, "Second Layer Index" ), maxLayer );
			sp->layer2Tint = std::uint32_t( get<ByteColor4>( o, "Second Layer Tint" ) );
			sp->layer2MaskIndex = std::min< quint8 >( get<quint8>( o, "Second Layer Mask Source" ), 3 );
			sp->blender1Index = std::min< quint8 >( get<quint8>( o, "First Blender Index" ), maxBlender );
			// "Lerp", "Additive", "Subtractive", "Multiplicative"
			sp->blender1Mode = get<quint8>( o, "First Blender Mode" ) & 3;
		}
		sp->layer3Active = get<bool>( o, "Third Layer Active" );
		if ( sp->layer3Active ) {
			sp->layer3Index = std::min< quint8 >( get<quint8>( o, "Third Layer Index" ), maxLayer );
			sp->layer3Tint = std::uint32_t( get<ByteColor4>( o, "Third Layer Tint" ) );
			sp->layer3MaskIndex = std::min< quint8 >( get<quint8>( o, "Third Layer Mask Source" ), 3 );
			sp->blender2Index = std::min< quint8 >( get<quint8>( o, "Second Blender Index" ), maxBlender );
			sp->blender2Mode = get<quint8>( o, "Second Blender Mode" ) & 3;
		}
		sp->clipThreshold = get<float>( o, "Emissive Clip Threshold" );
		sp->adaptiveEmittance = get<bool>( o, "Adaptive Emittance" );
		sp->luminousEmittance = get<float>( o, "Luminous Emittance" );
		sp->exposureOffset = get<float>( o, "Exposure Offset" );
		sp->enableAdaptiveLimits = get<bool>( o, "Enable Adaptive Limits" );
		sp->maxOffset = get<float>( o, "Max Offset Emittance" );
		sp->minOffset = get<float>( o, "Min Offset Emittance" );
		sp->ignoresFog = get<bool>( o, "Ignores Fog" );
		mat->layeredEmissiveSettings = sp;
		mat->setFlags( CE2Material::Flag_LayeredEmissivity, true );
	}

	if ( get<bool>( m, "Is Translucent" ) && ( o = getItem( m, "Translucency Settings" ) ) != nullptr ) {
		CE2Material::TranslucencySettings *	sp = bufs.constructObject< CE2Material::TranslucencySettings >();
		sp->isEnabled = true;
		sp->isThin = get<bool>( o, "Is Thin" );
		sp->flipBackFaceNormalsInVS = get<bool>( o, "Flip Back Face Normals In View Space" );
		sp->useSSS = get<bool>( o, "Use Subsurface Scattering" );
		if ( sp->useSSS ) {
			sp->sssWidth = get<float>( o, "Subsurface Scattering Width" );
			sp->sssStrength = get<float>( o, "Subsurface Scattering Strength" );
		}
		sp->transmissiveScale = get<float>( o, "Transmissive Scale" );
		sp->transmittanceWidth = get<float>( o, "Transmittance Width" );
		sp->specLobe0RoughnessScale = get<float>( o, "Spec Lobe 0 Roughness Scale" );
		sp->specLobe1RoughnessScale = get<float>( o, "Spec Lobe 1 Roughness Scale" );
		sp->sourceLayer = std::min< quint8 >( get<quint8>( o, "Transmittance Source Layer" ), maxLayer );
		mat->translucencySettings = sp;
		mat->setFlags( CE2Material::Flag_Translucency, true );
	}

	return mat;
}

