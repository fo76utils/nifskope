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
#include "io/material.h"

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
	bool	isFlipbook = false;
	const CE2Material::TextureSet *	textureSet = nullptr;
	const CE2Material::Material * material = reinterpret_cast< const CE2Material::Material * >(o);
	if ( o ) {
		name = material->name->c_str();
		color = material->color;
		colorMode = material->colorMode;
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
	if ( m ) {
		for ( auto c : m->childIter() )
			c->invalidateCondition();
	}

	const CE2MaterialDB *	materials = Game::GameManager::materials( Game::STARFIELD );
	const CE2Material *	material = nullptr;
	if ( materials ) {
		if ( !path.empty() )
			material = materials->findMaterial( path );
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
	const CE2Material::UVStream *	alphaLayerUVStream = nullptr;
	for ( int l = 0; l < 6; l++ ) {
		bool	layerEnabled = false;
		if ( material )
			layerEnabled = bool( material->layerMask & (1 << l) );
		setValue<bool>( m, QString("Layer %1 Enabled").arg(l), layerEnabled );
		if ( layerEnabled ) {
			loadSFLayer( getItem( itemToIndex( m ), QString("Layer %1").arg(l) ), material->layers[l] );
			if ( l == material->alphaSourceLayer && material->layers[l] )
				alphaLayerUVStream = material->layers[l]->uvStream;
			if ( l > 0 && material->blenders[l - 1] )
				loadSFBlender( getItem( itemToIndex( m ), QString("Blender %1").arg(l - 1) ), material->blenders[l - 1], material->layers[l]->uvStream );
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
		if ( hasOpacityComponent && ( o = getItem( itemToIndex(m), "Opacity Settings" ) ) != nullptr ) {
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
		if ( hasOpacity && ( o = getItem( itemToIndex(m), "Alpha Settings" ) ) != nullptr ) {
			setValue<float>( o, "Alpha Test Threshold", material->alphaThreshold );
			setValue<quint8>( o, "Opacity Source Layer", material->alphaSourceLayer );
			setValue<quint8>( o, "Alpha Blender Mode", material->alphaBlendMode );
			setValue<bool>( o, "Use Detail Blend Mask", bool(material->flags & CE2Material::Flag_AlphaDetailBlendMask) );
			setValue<bool>( o, "Use Vertex Color", bool(material->flags & CE2Material::Flag_AlphaVertexColor) );
			if ( material->flags & CE2Material::Flag_AlphaVertexColor )
				setValue<quint8>( o, "Vertex Color Channel", material->alphaVertexColorChannel );
			loadSFUVStream( getItem( itemToIndex(o), "Opacity UV Stream" ), material->alphaUVStream, alphaLayerUVStream );
			setValue<float>( o, "Height Blend Threshold", material->alphaHeightBlendThreshold );
			setValue<float>( o, "Height Blend Factor", material->alphaHeightBlendFactor );
			setValue<float>( o, "Position", material->alphaPosition );
			setValue<float>( o, "Contrast", material->alphaContrast );
			setValue<bool>( o, "Use Dithered Transparency", bool(material->flags & CE2Material::Flag_DitheredTransparency) );
		}
	}
	bool	isEffect = false;
	if ( material )
		isEffect = bool( material->flags & CE2Material::Flag_IsEffect );
	setValue<bool>( m, "Is Effect", isEffect );
	if ( isEffect && ( o = getItem( itemToIndex(m), "Effect Settings" ) ) != nullptr ) {
		const CE2Material::EffectSettings *	sp = material->effectSettings;
		setValue<bool>( o, "Use Falloff", bool(sp->flags & CE2Material::EffectFlag_UseFalloff) );
		setValue<bool>( o, "Use RGB Falloff", bool(sp->flags & CE2Material::EffectFlag_UseRGBFalloff) );
		if ( sp->flags & ( CE2Material::EffectFlag_UseFalloff | CE2Material::EffectFlag_UseRGBFalloff ) ) {
			setValue<float>( o, "Falloff Start Angle", sp->falloffStartAngle );
			setValue<float>( o, "Falloff Stop Angle", sp->falloffStopAngle );
			setValue<float>( o, "Falloff Start Opacity", sp->falloffStartOpacity );
			setValue<float>( o, "Falloff Stop Opacity", sp->falloffStopOpacity );
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
		setValue<quint16>( o, "Depth Bias In Ulp", quint16(sp->depthBias) );
	}
	bool	isDecal = false;
	if ( material )
		isDecal = bool( material->flags & CE2Material::Flag_IsDecal );
	setValue<bool>( m, "Is Decal", isDecal );
	if ( isDecal && ( o = getItem( itemToIndex(m), "Decal Settings" ) ) != nullptr ) {
		const CE2Material::DecalSettings *	sp = material->decalSettings;
		setValue<float>( o, "Material Overall Alpha", sp->decalAlpha );
		setValue<quint32>( o, "Write Mask", sp->writeMask );
		setValue<bool>( o, "Is Planet", sp->isPlanet );
		setValue<bool>( o, "Is Projected", sp->isProjected );
		if ( sp->isProjected ) {
			setValue<bool>( o, "Use Parallax Occlusion Mapping", sp->useParallaxMapping );
			setValue<QString>( o, "Surface Height Map", sp->surfaceHeightMap->c_str() );
			setValue<float>( o, "Parallax Occlusion Scale", sp->parallaxOcclusionScale );
			setValue<bool>( o, "Parallax Occlusion Shadows", sp->parallaxOcclusionShadows );
			setValue<quint8>( o, "Max Parralax Occlusion Steps", sp->maxParallaxSteps );
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
	if ( isWater && ( o = getItem( itemToIndex(m), "Water Settings" ) ) != nullptr ) {
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
	if ( isEmissive && ( o = getItem( itemToIndex(m), "Emissive Settings" ) ) != nullptr ) {
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
	bool	isTranslucent = false;
	if ( material )
		isTranslucent = bool( material->flags & CE2Material::Flag_Translucency );
	setValue<bool>( m, "Is Translucent", isTranslucent );
	if ( isTranslucent && ( o = getItem( itemToIndex(m), "Translucency Settings" ) ) != nullptr ) {
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
	if ( !p )
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

	QVector< quint32 >	sf1;
	QVector< quint32 >	sf2;
	if ( mat.bZBufferWrite )
		sf1.append( 3166356979U );
	if ( mat.bZBufferTest )
		sf1.append( 1740048692U );
	if ( mat.bDecal )
		sf1.append( 3849131744U );
	if ( mat.bTwoSided )
		sf1.append( 759557230U );
	if ( mat.bDecalNoFade )
		sf1.append( 2994043788U );
	if ( mat.bRefraction )
		sf1.append( 1957349758U );
	if ( mat.bRefractionFalloff )
		sf1.append( 902349195U );
	if ( mat.bEnvironmentMapping )
		sf1.append( 2893749418U );
	if ( mat.bGrayscaleToPaletteColor )
		sf1.append( 442246519U );
	if ( mat.bGlowmap )
		sf1.append( 2399422528U );
	if ( bgsm ) {
		if ( bgsm->bEmitEnabled )
			sf2.append( 2262553490U );
		if ( bgsm->bModelSpaceNormals )
			sf2.append( 2548465567U );
		if ( bgsm->bExternalEmittance )
			sf2.append( 2150459555U );
		if ( bgsm->bCastShadows )
			sf2.append( 1563274220U );
		if ( bgsm->bHair )
			sf2.append( 1264105798U );
		if ( bgsm->bFacegen )
			sf2.append( 314919375U );
		if ( bgsm->bSkinTint )
			sf2.append( 1483897208U );
		if ( bgsm->bPBR )
			sf2.append( 731263983U );
	}
	if ( bgem ) {
		if ( bgem->bBloodEnabled )
			sf2.append( 2078326675U );
		if ( bgem->bEffectLightingEnabled )
			sf2.append( 3473438218U );
		if ( bgem->bFalloffEnabled )
			sf2.append( 3980660124U );
		if ( bgem->bFalloffColorEnabled )
			sf2.append( 3448946507U );
		if ( bgem->bGrayscaleToPaletteAlpha )
			sf2.append( 2901038324U );
		if ( bgem->bSoftEnabled )
			sf2.append( 3503164976U );
		if ( bgem->bEffectPbrSpecular )
			sf2.append( 731263983U );
	}
	setValue<quint32>( p, "Num SF1", quint32( sf1.size() ) );
	NifItem *	o = getItem( itemToIndex(p), "SF1" );
	if ( o ) {
		updateArraySize( o );
		o->setArray<quint32>( sf1 );
	}
	setValue<quint32>( p, "Num SF2", quint32( sf2.size() ) );
	o = getItem( itemToIndex(p), "SF2" );
	if ( o ) {
		updateArraySize( o );
		o->setArray<quint32>( sf2 );
	}

	setValue<Vector2>( p, "UV Offset", Vector2( mat.fUOffset, mat.fVOffset ) );
	setValue<Vector2>( p, "UV Scale", Vector2( mat.fUScale, mat.fVScale ) );
	setValue<quint32>( p, "Texture Clamp Mode", quint32( mat.bTileU ) | ( quint32( mat.bTileV ) << 1 ) );
	setValue<float>( p, "Alpha", mat.fAlpha );

	if ( bgsm ) {
		for ( qsizetype i = 0; i < 10; i++ ) {
			if ( mat.textureList.size() > i )
				setValue<QString>( p, QString("Texture %1").arg(i), mat.textureList[i] );
			else
				setValue<QString>( p, QString("Texture %1").arg(i), "" );
		}
		setValue<Color3>( p, "Emissive Color", mat.cEmittanceColor );
		setValue<float>( p, "Emissive Multiple", bgsm->fEmittanceMult );
		setValue<float>( p, "Refraction Strength", mat.fRefractionPower );
		setValue<float>( p, "Smoothness", bgsm->fSmoothness );
		setValue<Color3>( p, "Specular Color", bgsm->cSpecularColor );
		setValue<float>( p, "Specular Strength", bgsm->fSpecularMult );
		setValue<float>( p, "Grayscale to Palette Scale", bgsm->fGrayscaleToPaletteScale );
		setValue<float>( p, "Fresnel Power", bgsm->fFresnelPower );
		setValue<bool>( p, "Do Translucency", bgsm->bTranslucency );
	} else if ( bgem ) {
		if ( mat.textureList.size() > 0 )
			setValue<QString>( p, "Source Texture", mat.textureList[0] );
		else
			setValue<QString>( p, "Source Texture", "" );
		setValue<float>( p, "Lighting Influence", bgem->fLightingInfluence );
		setValue<quint8>( p, "Env Map Min LOD", bgem->iEnvmapMinLOD );
		setValue<float>( p, "Falloff Start Angle", bgem->fFalloffStartAngle );
		setValue<float>( p, "Falloff Stop Angle", bgem->fFalloffStopAngle );
		setValue<float>( p, "Falloff Start Opacity ", bgem->fFalloffStartOpacity );
		setValue<float>( p, "Falloff Stop Opacity", bgem->fFalloffStopOpacity );
		setValue<float>( p, "Refraction Power", mat.fRefractionPower );
		setValue<Color4>( p, "Base Color", Color4( bgem->cBaseColor ) );
		setValue<float>( p, "Base Color Scale", bgem->fBaseColorScale );
		setValue<float>( p, "Soft Falloff Depth", bgem->fSoftDepth );
		if ( mat.textureList.size() > 1 )
			setValue<QString>( p, "Greyscale Texture", mat.textureList[1] );
		else
			setValue<QString>( p, "Greyscale Texture", "" );
		if ( mat.textureList.size() > 2 )
			setValue<QString>( p, "Env Map Texture", mat.textureList[2] );
		else
			setValue<QString>( p, "Env Map Texture", "" );
		if ( mat.textureList.size() > 3 )
			setValue<QString>( p, "Normal Texture", mat.textureList[3] );
		else
			setValue<QString>( p, "Normal Texture", "" );
		if ( mat.textureList.size() > 4 )
			setValue<QString>( p, "Env Mask Texture", mat.textureList[4] );
		else
			setValue<QString>( p, "Env Mask Texture", "" );
		if ( mat.textureList.size() > 5 )
			setValue<QString>( p, "Reflectance Texture", mat.textureList[5] );
		else
			setValue<QString>( p, "Reflectance Texture", "" );
		if ( mat.textureList.size() > 6 )
			setValue<QString>( p, "Lighting Texture", mat.textureList[6] );
		else
			setValue<QString>( p, "Lighting Texture", "" );
		if ( mat.textureList.size() > 7 )
			setValue<QString>( p, "Emit Gradient Texture", mat.textureList[7] );
		else
			setValue<QString>( p, "Emit Gradient Texture", "" );
	}
}
