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
	if ( !parent )
		return;
	if ( !o )
		o = &CE2Material::defaultBlender;

	const CE2Material::Blender *	blender = reinterpret_cast< const CE2Material::Blender * >(o);
	const char *	name = blender->name;
	const char *	maskTexture = blender->texturePath->data();
	bool	maskTextureReplacementEnabled = blender->textureReplacementEnabled;
	std::uint32_t	maskTextureReplacement = blender->textureReplacement;
	const CE2Material::UVStream *	maskUVStream = blender->uvStream;
	unsigned char	blendMode = blender->blendMode;
	unsigned char	vertexColorChannel = blender->colorChannel;
	float	floatParams[5];
	for ( int i = 0; i < 5; i++ )
		floatParams[i] = blender->floatParams[i];
	bool	boolParams[8];
	for ( int i = 0; i < 8; i++ )
		boolParams[i] = blender->boolParams[i];

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

void NifModel::loadSFLayer( NifItem * parent, const void * o )
{
	if ( !parent )
		return;
	if ( !o )
		o = &CE2Material::defaultLayer;

	const CE2Material::Layer *	layer = reinterpret_cast< const CE2Material::Layer * >(o);
	const char *	name = layer->name;
	const CE2Material::UVStream *	uvStream = layer->uvStream;
	const CE2Material::Material *	material = layer->material;

	setValue<QString>( parent, "Name", name );
	loadSFUVStream( getItem( parent, "UV Stream" ), uvStream );
	loadSFMaterial( getItem( parent, "Material" ), material );
}

void NifModel::loadSFMaterial( NifItem * parent, const void * o )
{
	if ( !parent )
		return;
	if ( !o )
		o = &CE2Material::defaultMaterial;

	const CE2Material::Material *	material = reinterpret_cast< const CE2Material::Material * >(o);
	const char *	name = material->name;
	FloatVector4	color = material->color;
	unsigned char	colorMode = material->colorModeFlags;
	bool	isFlipbook = bool(material->flipbookFlags & 1);
	const CE2Material::TextureSet *	textureSet = material->textureSet;

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
	loadSFTextureSet( getItem( parent, "Texture Set" ), textureSet );
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
	if ( !o )
		o = &CE2Material::defaultTextureSet;

	const CE2Material::TextureSet *	textureSet = reinterpret_cast< const CE2Material::TextureSet * >(o);
	const char *	name = textureSet->name;
	float	floatParam = textureSet->floatParam;
	unsigned char	resolutionHint = textureSet->resolutionHint;
	bool			disableMipBiasHint = textureSet->disableMipBiasHint;
	std::uint32_t	texturePathMask = textureSet->texturePathMask;
	std::uint32_t	textureReplacementMask = textureSet->textureReplacementMask;

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
	if ( !parent )
		return;
	if ( !o )
		o = &CE2Material::defaultUVStream;

	const CE2Material::UVStream *	uvStream = reinterpret_cast< const CE2Material::UVStream * >(o);
	const char *	name = uvStream->name;
	FloatVector4	scaleAndOffset = uvStream->scaleAndOffset;
	unsigned char	textureAddressMode = uvStream->textureAddressMode;
	unsigned char	channel = uvStream->channel;

	setValue<QString>( parent, "Name", name );
	setValue<Vector2>( parent, "Scale", Vector2( scaleAndOffset[0], scaleAndOffset[1] ) );
	setValue<Vector2>( parent, "Offset", Vector2( scaleAndOffset[2], scaleAndOffset[3] ) );
	setValue<quint8>( parent, "Texture Address Mode", textureAddressMode );
	setValue<quint8>( parent, "Channel", channel );
}

void NifModel::loadSFMaterial( const QModelIndex & parent, const void * matPtr, int lodLevel )
{
	if ( !matPtr )
		matPtr = &CE2Material::defaultLayeredMaterial;

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

	if ( lodLevel > 0 ) {
		for ( int i = lodLevel - 1; i >= 0; i-- ) {
			if ( i <= 2 && material->lodMaterials[i] ) {
				material = material->lodMaterials[i];
				break;
			}
		}
	}

	setValue<QString>( m, "Name", material->name );
	setValue<bool>( m, "Is Modified", false );
	{
		std::uint32_t	layerMask = 0;
		std::uint32_t	blenderMask = 0;
		for ( int l = 0; l < CE2Material::maxLayers; l++ ) {
			if ( ( material->layerMask & ( 1U << l ) ) && material->layers[l] )
				layerMask |= std::uint32_t( 1U << l );
		}
		for ( int l = 0; l < CE2Material::maxBlenders; l++ ) {
			if ( material->blenders[l] )
				blenderMask |= std::uint32_t( 1U << l );
		}
		setValue<quint32>( m, "Layer Enable Mask", layerMask );
		for ( int l = 0; layerMask; l++, layerMask = layerMask >> 1 ) {
			if ( layerMask & 1 )
				loadSFLayer( getItem( m, QString("Layer %1").arg(l + 1) ), material->layers[l] );
		}
		setValue<quint32>( m, "Blender Enable Mask", blenderMask );
		for ( int l = 0; blenderMask; l++, blenderMask = blenderMask >> 1 ) {
			if ( blenderMask & 1 )
				loadSFBlender( getItem( m, QString("Blender %1").arg(l + 1) ), material->blenders[l] );
		}
	}
	setValue<QString>( m, "Shader Model", QString( CE2Material::shaderModelNames[material->shaderModel] ) );
	setValue<quint8>( m, "Shader Route", material->shaderRoute );
	setValue<bool>( m, "Two Sided", bool( material->flags & CE2Material::Flag_TwoSided ) );
	setValue<quint8>( m, "Physics Material Type", material->physicsMaterialType );
	NifItem *	o;
	if ( material->shaderRoute == 1 ) {	// effect material
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
		bool	hasOpacity = bool( material->flags & CE2Material::Flag_HasOpacity );
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
	bool	useDetailBlendMask = bool( material->flags & CE2Material::Flag_UseDetailBlender );
	setValue<bool>( m, "Detail Blend Mask Supported", useDetailBlendMask );
	if ( useDetailBlendMask && ( o = getItem( m, "Detail Blender Settings" ) ) != nullptr ) {
		const CE2Material::DetailBlenderSettings *	sp = material->detailBlenderSettings;
		loadSFTextureWithReplacement( getItem( o, "Texture" ), sp->texturePath->data(), sp->textureReplacementEnabled, sp->textureReplacement );
		loadSFUVStream( getItem( o, "UV Stream" ), sp->uvStream );
	}
	bool	isEffect = bool( material->flags & CE2Material::Flag_IsEffect );
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
		layeredEdgeFalloff = bool( material->layeredEdgeFalloff );
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
	bool	isDecal = bool( material->flags & CE2Material::Flag_IsDecal );
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
	bool	isWater = bool( material->flags & CE2Material::Flag_IsWater );
	setValue<bool>( m, "Is Water", isWater );
	if ( isWater && ( o = getItem( m, "Water Settings" ) ) != nullptr ) {
		const CE2Material::WaterSettings *	sp = material->waterSettings;
		setValue<float>( o, "Water Edge Falloff", sp->waterEdgeFalloff );
		setValue<float>( o, "Water Wetness Max Depth", sp->waterWetnessMaxDepth );
		setValue<float>( o, "Water Edge Normal Falloff", sp->waterEdgeNormalFalloff );
		setValue<float>( o, "Water Depth Blur", sp->waterDepthBlur );
		setValue<float>( o, "Water Refraction Magnitude", sp->reflectance[3] );
		setValue<Vector3>( o, "Phytoplankton Reflectance Color", Vector3( sp->phytoplanktonReflectance ) );
		setValue<Vector3>( o, "Sediment Reflectance Color", Vector3( sp->sedimentReflectance ) );
		setValue<Vector3>( o, "Yellow Matter Reflectance Color", Vector3( sp->yellowMatterReflectance ) );
		setValue<float>( o, "Max Concentration Plankton", sp->phytoplanktonReflectance[3] );
		setValue<float>( o, "Max Concentration Sediment", sp->sedimentReflectance[3] );
		setValue<float>( o, "Max Concentration Yellow Matter", sp->yellowMatterReflectance[3] );
		setValue<Vector3>( o, "Reflectance Color", Vector3( sp->reflectance ) );
		setValue<bool>( o, "Low LOD", sp->lowLOD );
		setValue<bool>( o, "Placed Water", sp->placedWater );
	}
	bool	isEmissive = bool( material->flags & CE2Material::Flag_Emissive );
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
	bool	layeredEmissivity = bool( material->flags & CE2Material::Flag_LayeredEmissivity );
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
	bool	isTranslucent = bool( material->flags & CE2Material::Flag_Translucency );
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
	bool	isHair = false;
	if ( ( material->flags & CE2Material::Flag_IsHair ) && material->hairSettings )
		isHair = material->hairSettings->isEnabled;
	setValue<bool>( m, "Is Hair", isHair );
	if ( isHair && ( o = getItem( m, "Hair Settings" ) ) != nullptr ) {
		const CE2Material::HairSettings *	sp = material->hairSettings;
		setValue<bool>( o, "Is Spiky Hair", sp->isSpikyHair );
		setValue<float>( o, "Spec Scale", sp->specScale );
		setValue<float>( o, "Specular Transmission Scale", sp->specularTransmissionScale );
		setValue<float>( o, "Direct Transmission Scale", sp->directTransmissionScale );
		setValue<float>( o, "Diffuse Transmission Scale", sp->diffuseTransmissionScale );
		setValue<float>( o, "Roughness", sp->roughness );
		setValue<float>( o, "Contact Shadow Softening", sp->contactShadowSoftening );
		setValue<float>( o, "Backscatter Strength", sp->backscatterStrength );
		setValue<float>( o, "Backscatter Wrap", sp->backscatterWrap );
		setValue<float>( o, "Variation Strength", sp->variationStrength );
		setValue<float>( o, "Indirect Specular Scale", sp->indirectSpecularScale );
		setValue<float>( o, "Indirect Specular Transmission Scale", sp->indirectSpecularTransmissionScale );
		setValue<float>( o, "Indirect Spec Roughness", sp->indirectSpecRoughness );
		setValue<float>( o, "Edge Mask Contrast", sp->edgeMaskContrast );
		setValue<float>( o, "Edge Mask Min", sp->edgeMaskMin );
		setValue<float>( o, "Edge Mask Distance Min", sp->edgeMaskDistanceMin );
		setValue<float>( o, "Edge Mask Distance Max", sp->edgeMaskDistanceMax );
		setValue<float>( o, "Max Depth Offset", sp->maxDepthOffset );
		setValue<float>( o, "Dither Scale", sp->ditherScale );
		setValue<float>( o, "Dither Distance Min", sp->ditherDistanceMin );
		setValue<float>( o, "Dither Distance Max", sp->ditherDistanceMax );
		setValue<Vector3>( o, "Tangent", Vector3( sp->tangent ) );
		setValue<float>( o, "Tangent Bend", sp->tangent[3] );
		setValue<quint8>( o, "Depth Offset Mask Vertex Color Channel", sp->depthOffsetMaskVertexColorChannel );
		setValue<quint8>( o, "AO Vertex Color Channel", sp->aoVertexColorChannel );
	}
	bool	isVegetation = false;
	if ( ( material->flags & CE2Material::Flag_IsVegetation ) && material->vegetationSettings )
		isVegetation = material->vegetationSettings->isEnabled;
	setValue<bool>( m, "Is Vegetation", isVegetation );
	if ( isVegetation && ( o = getItem( m, "Vegetation Settings" ) ) != nullptr ) {
		const CE2Material::VegetationSettings *	sp = material->vegetationSettings;
		setValue<float>( o, "Leaf Frequency", sp->leafFrequency );
		setValue<float>( o, "Leaf Amplitude", sp->leafAmplitude );
		setValue<float>( o, "Branch Flexibility", sp->branchFlexibility );
		setValue<float>( o, "Trunk Flexibility", sp->trunkFlexibility );
		setValue<float>( o, "Terrain Blend Strength (Deprecated)", sp->terrainBlendStrength );
		setValue<float>( o, "Terrain Blend Gradient Factor (Deprecated)", sp->terrainBlendGradientFactor );
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
		Vector2	uvScale = get<Vector2>( parent, "Scale" );
		Vector2	uvOffset = get<Vector2>( parent, "Offset" );
		uvStream->scaleAndOffset = FloatVector4( uvScale[0], uvScale[1], uvOffset[0], uvOffset[1] );
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

	quint32	layerMask = get<quint32>( m, "Layer Enable Mask" );
	for ( int l = 0; l < CE2Material::maxLayers; l++ ) {
		if ( !( layerMask & ( 1U << l ) ) )
			continue;

		mat->layers[l] = reinterpret_cast< const CE2Material::Layer * >(
							createSFLayer( bufs, getItem( m, QString( "Layer %1" ).arg( l + 1 ) ) ) );
		if ( mat->layers[l] ) {
			const_cast< CE2Material::Layer * >( mat->layers[l] )->parent = mat;
			mat->layerMask |= std::uint32_t( 1U << l );
		}
	}
	quint32	blenderMask = get<quint32>( m, "Blender Enable Mask" );
	for ( int l = 0; l < CE2Material::maxBlenders; l++ ) {
		if ( !( blenderMask & ( 1U << l ) ) )
			continue;
		mat->blenders[l] = reinterpret_cast< const CE2Material::Blender * >(
								createSFBlender( bufs, getItem( m, QString( "Blender %1" ).arg( l + 1 ) ) ) );
		if ( mat->blenders[l] )
			const_cast< CE2Material::Blender * >( mat->blenders[l] )->parent = mat;
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
			// "Top", "Middle", "Bottom"
			sp->renderLayer = std::min< quint8 >( get<quint8>( o, "Render Layer" ), 2 );
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
		sp->phytoplanktonReflectance = FloatVector4( get<Vector3>( o, "Phytoplankton Reflectance Color" ) );
		sp->sedimentReflectance = FloatVector4( get<Vector3>( o, "Sediment Reflectance Color" ) );
		sp->yellowMatterReflectance = FloatVector4( get<Vector3>( o, "Yellow Matter Reflectance Color" ) );
		sp->phytoplanktonReflectance[3] = get<float>( o, "Max Concentration Plankton" );
		sp->sedimentReflectance[3] = get<float>( o, "Max Concentration Sediment" );
		sp->yellowMatterReflectance[3] = get<float>( o, "Max Concentration Yellow Matter" );
		sp->reflectance.blendValues( FloatVector4( get<Vector3>( o, "Reflectance Color" ) ), 0x07 );
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
		sp->layer1Tint = std::uint32_t( FloatVector4( get<Color4>( o, "First Layer Tint" ) ) * 255.0f );
		// "None", "Blender 0", "Blender 1", "Blender 2"
		sp->layer1MaskIndex = std::min< quint8 >( get<quint8>( o, "First Layer Mask Source" ), 3 );
		sp->layer2Active = get<bool>( o, "Second Layer Active" );
		if ( sp->layer2Active ) {
			sp->layer2Index = std::min< quint8 >( get<quint8>( o, "Second Layer Index" ), maxLayer );
			sp->layer2Tint = std::uint32_t( FloatVector4( get<Color4>( o, "Second Layer Tint" ) ) * 255.0f );
			sp->layer2MaskIndex = std::min< quint8 >( get<quint8>( o, "Second Layer Mask Source" ), 3 );
			sp->blender1Index = std::min< quint8 >( get<quint8>( o, "First Blender Index" ), maxBlender );
			// "Lerp", "Additive", "Subtractive", "Multiplicative"
			sp->blender1Mode = get<quint8>( o, "First Blender Mode" ) & 3;
		}
		sp->layer3Active = get<bool>( o, "Third Layer Active" );
		if ( sp->layer3Active ) {
			sp->layer3Index = std::min< quint8 >( get<quint8>( o, "Third Layer Index" ), maxLayer );
			sp->layer3Tint = std::uint32_t( FloatVector4( get<Color4>( o, "Third Layer Tint" ) ) * 255.0f );
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

	if ( get<bool>( m, "Is Hair" ) && ( o = getItem( m, "Hair Settings" ) ) != nullptr ) {
		CE2Material::HairSettings *	sp = bufs.constructObject< CE2Material::HairSettings >();
		sp->isEnabled = true;
		sp->isSpikyHair = get<bool>( o, "Is Spiky Hair" );
		sp->specScale = get<float>( o, "Spec Scale" );
		sp->specularTransmissionScale = get<float>( o, "Specular Transmission Scale" );
		sp->directTransmissionScale = get<float>( o, "Direct Transmission Scale" );
		sp->diffuseTransmissionScale = get<float>( o, "Diffuse Transmission Scale" );
		sp->roughness = get<float>( o, "Roughness" );
		sp->contactShadowSoftening = get<float>( o, "Contact Shadow Softening" );
		sp->backscatterStrength = get<float>( o, "Backscatter Strength" );
		sp->backscatterWrap = get<float>( o, "Backscatter Wrap" );
		sp->variationStrength = get<float>( o, "Variation Strength" );
		sp->indirectSpecularScale = get<float>( o, "Indirect Specular Scale" );
		sp->indirectSpecularTransmissionScale = get<float>( o, "Indirect Specular Transmission Scale" );
		sp->indirectSpecRoughness = get<float>( o, "Indirect Spec Roughness" );
		sp->edgeMaskContrast = get<float>( o, "Edge Mask Contrast" );
		sp->edgeMaskMin = get<float>( o, "Edge Mask Min" );
		sp->edgeMaskDistanceMin = get<float>( o, "Edge Mask Distance Min" );
		sp->edgeMaskDistanceMax = get<float>( o, "Edge Mask Distance Max" );
		sp->maxDepthOffset = get<float>( o, "Max Depth Offset" );
		sp->ditherScale = get<float>( o, "Dither Scale" );
		sp->ditherDistanceMin = get<float>( o, "Dither Distance Min" );
		sp->ditherDistanceMax = get<float>( o, "Dither Distance Max" );
		sp->tangent = FloatVector4( get<Vector3>( o, "Tangent" ) );
		sp->tangent[3] = get<float>( o, "Tangent Bend" );
		sp->depthOffsetMaskVertexColorChannel = get<quint8>( o, "Depth Offset Mask Vertex Color Channel" ) & 3;
		sp->aoVertexColorChannel = get<quint8>( o, "AO Vertex Color Channel" ) & 3;
		mat->hairSettings = sp;
		mat->setFlags( CE2Material::Flag_IsHair, true );
	}

	if ( get<bool>( m, "Is Vegetation" ) && ( o = getItem( m, "Vegetation Settings" ) ) != nullptr ) {
		CE2Material::VegetationSettings *	sp = bufs.constructObject< CE2Material::VegetationSettings >();
		sp->isEnabled = true;
		sp->leafFrequency = get<float>( o, "Leaf Frequency" );
		sp->leafAmplitude = get<float>( o, "Leaf Amplitude" );
		sp->branchFlexibility = get<float>( o, "Branch Flexibility" );
		sp->trunkFlexibility = get<float>( o, "Trunk Flexibility" );
		sp->terrainBlendStrength = get<float>( o, "Terrain Blend Strength (Deprecated)" );
		sp->terrainBlendGradientFactor = get<float>( o, "Terrain Blend Gradient Factor (Deprecated)" );
		mat->vegetationSettings = sp;
		mat->setFlags( CE2Material::Flag_IsVegetation, true );
	}

	return mat;
}

