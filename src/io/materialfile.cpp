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

#include "material.h"
#include "model/nifmodel.h"
#include "fp32vec8.hpp"

#include <QBuffer>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QSettings>


//! @file material.cpp BGSM/BGEM file I/O

#define BGSM 0x4D534742
#define BGEM 0x4D454742

Material::Material()
{
}

bool Material::openFile( const QString & name, const NifModel * nif, const QModelIndex & index )
{
	if ( !nif )
		return false;

	QByteArray	data;

	if ( index.isValid() && nif->getBSVersion() >= 130 )
		createMaterialData( data, nif, index );
	if ( data.isEmpty() && !name.isEmpty() )
		nif->getResourceFile( data, name, "materials", "" );

	if ( data.isEmpty() )
		return false;

	QBuffer f( &data );
	if ( f.open( QIODevice::ReadOnly ) ) {
		QDataStream	in;
		in.setDevice( &f );
		in.setByteOrder( QDataStream::LittleEndian );
		in.setFloatingPointPrecision( QDataStream::SinglePrecision );

		quint32 magic;
		in >> magic;

		if ( magic != BGSM && magic != BGEM )
			return false;

		return readFile( in );
	}

	return false;
}

bool Material::readFile( QDataStream & in )
{
	in >> version;

	in >> tileFlags;
	bTileU = (tileFlags & 0x2) != 0;
	bTileV = (tileFlags & 0x1) != 0;

	in >> fUOffset >> fVOffset >> fUScale >> fVScale;
	in >> fAlpha;
	in >> bAlphaBlend >> iAlphaSrc >> iAlphaDst;
	in >> iAlphaTestRef;
	in >> bAlphaTest >> bZBufferWrite >> bZBufferTest;
	in >> bScreenSpaceReflections >> bWetnessControl_ScreenSpaceReflections;
	in >> bDecal >> bTwoSided >> bDecalNoFade >> bNonOccluder;
	in >> bRefraction >> bRefractionFalloff >> fRefractionPower;
	in >> bEnvironmentMapping;
	if ( version < 10 )
		in >> fEnvironmentMappingMaskScale;

	in >> bGrayscaleToPaletteColor;

	if ( version >= 6 )
		in >> ucMaskWrites;

	return in.status() == QDataStream::Ok;
}


ShaderMaterial::ShaderMaterial( const QString & name, const NifModel * nif, const QModelIndex & index )
	: Material()
{
	isBGSM = true;
	readable = openFile( name, nif, index );
}

bool ShaderMaterial::readFile( QDataStream & in )
{
	Material::readFile( in );

	size_t numTex = (version >= 17) ? 10 : 9;
	for ( size_t i = 0; i < numTex; i++ ) {
		char * str;
		in >> str;
		textureList << QString( str );
	}

	in >> bEnableEditorAlphaRef;
	if ( version >= 8 ) {
		in >> bTranslucency >> bTranslucencyThickObject >> bTranslucencyMixAlbedoWithSubsurfaceCol;
		in >> cTranslucencySubsurfaceColor[0] >> cTranslucencySubsurfaceColor[1] >> cTranslucencySubsurfaceColor[2];
		in >> fTranslucencyTransmissiveScale >> fTranslucencyTurbulence;
	}
	else
		in >> bRimLighting >> fRimPower >> fBacklightPower >> bSubsurfaceLighting >> fSubsurfaceLightingRolloff;

	in >> bSpecularEnabled;
	in >> cSpecularColor[0] >> cSpecularColor[1] >> cSpecularColor[2];
	in >> fSpecularMult >> fSmoothness;
	in >> fFresnelPower;
	in >> fWetnessControl_SpecScale >> fWetnessControl_SpecPowerScale >> fWetnessControl_SpecMinvar;
	if ( version < 10 )
		in >> fWetnessControl_EnvMapScale;

	in >> fWetnessControl_FresnelPower >> fWetnessControl_Metalness;

	if ( version > 2 )
		in >> bPBR;

	if ( version >= 9 )
		in >> bCustomPorosity >> fPorosityValue;

	char * rootMaterialStr;
	in >> rootMaterialStr;
	sRootMaterialPath = QString( rootMaterialStr );

	in >> bAnisoLighting >> bEmitEnabled;

	if ( bEmitEnabled )
		in >> cEmittanceColor[0] >> cEmittanceColor[1] >> cEmittanceColor[2];

	in >> fEmittanceMult >> bModelSpaceNormals;
	in >> bExternalEmittance;
	if ( version >= 12 )
		in >> fLumEmittance;
	if ( version >= 13 )
		in >> bUseAdaptativeEmissive >> fAdaptativeEmissive_ExposureOffset >> fAdaptativeEmissive_FinalExposureMin >> fAdaptativeEmissive_FinalExposureMax;

	if ( version < 8 )
		in >> bBackLighting;
	in >> bReceiveShadows >> bHideSecret >> bCastShadows;
	in >> bDissolveFade >> bAssumeShadowmask >> bGlowmap;

	if ( version < 7 )
		in >> bEnvironmentMappingWindow >> bEnvironmentMappingEye;
	in >> bHair >> cHairTintColor[0] >> cHairTintColor[1] >> cHairTintColor[2];

	in >> bTree >> bFacegen >> bSkinTint >> bTessellate;
	if ( version < 3 )
		in >> fDisplacementTextureBias >> fDisplacementTextureScale >>
		fTessellationPnScale >> fTessellationBaseFactor >> fTessellationFadeDistance;
	in >> fGrayscaleToPaletteScale >> bSkewSpecularAlpha;

	if ( version >= 3 ) {
		in >> bTerrain;
		if ( bTerrain ) {
			if ( version == 3 )
				in.skipRawData(4);
			in >> fTerrainThresholdFalloff >> fTerrainTilingDistance >> fTerrainRotationAngle;
		}
	}

	return in.status() == QDataStream::Ok;
}

EffectMaterial::EffectMaterial( const QString & name, const NifModel * nif, const QModelIndex & index )
	: Material()
{
	isBGEM = true;
	readable = openFile( name, nif, index );
}

bool EffectMaterial::readFile( QDataStream & in )
{
	Material::readFile( in );

	size_t numTex = ( version >= 10 ) ? ( version <= 20 ? 8 : 10 ) : 5;
	for ( size_t i = 0; i < numTex; i++ ) {
		char * str;
		in >> str;
		textureList << QString( str );
	}

	if ( version >= 10 ) {
		if ( version > 20 ) {
			in >> bGlassEnabled;
			if ( bGlassEnabled ) {
				in >> cGlassFresnelColor[0] >> cGlassFresnelColor[1] >> cGlassFresnelColor[2];
				// FIXME: the order of these may be incorrect
				in >> fGlassRefractionScaleBase;
				in >> fGlassBlurScaleBase;
				if ( version > 21 )
					in >> fGlassBlurScaleFactor;
			}
		}
		in >> bEnvironmentMapping;
		in >> fEnvironmentMappingMaskScale;
	}

	in >> bBloodEnabled >> bEffectLightingEnabled;
	in >> bFalloffEnabled >> bFalloffColorEnabled;
	in >> bGrayscaleToPaletteAlpha >> bSoftEnabled;
	in >> cBaseColor[0] >> cBaseColor[1] >> cBaseColor[2];

	in >> fBaseColorScale;
	in >> fFalloffStartAngle >> fFalloffStopAngle;
	in >> fFalloffStartOpacity >> fFalloffStopOpacity;
	in >> fLightingInfluence >> iEnvmapMinLOD >> fSoftDepth;

	if ( version >= 11 ) {
		in >> cEmittanceColor[0] >> cEmittanceColor[1] >> cEmittanceColor[2];

		if ( version >= 15 ) {
			in >> fAdaptativeEmissive_ExposureOffset >> fAdaptativeEmissive_FinalExposureMin >> fAdaptativeEmissive_FinalExposureMax;

			if ( version >= 16 )
				in >> bGlowmap;

			if ( version >= 20 )
				in >> bEffectPbrSpecular;
		}
	}

	return in.status() == QDataStream::Ok;
}


void Material::createMaterialData( QByteArray & data, const NifModel * nif, const QModelIndex & index )
{
	if ( !( nif && nif->getBSVersion() >= 130 && index.isValid() ) )
		return;
	const NifItem *	m = nif->getItem( index );
	if ( !m )
		return;
	bool	isEffect = nif->isNiBlock( m, "BSEffectShaderProperty" );
	m = nif->getItem( m, "Material" );
	if ( !( m && nif->get<bool>( m, "Is Modified" ) ) )
		return;

	QBuffer	f( &data );
	if ( !f.open( QIODevice::WriteOnly ) )
		return;
	QDataStream	s( &f );
	s.setByteOrder( QDataStream::LittleEndian );
	s.setFloatingPointPrecision( QDataStream::SinglePrecision );

	s << quint32( !isEffect ? BGSM : BGEM );
	quint32	version = std::max<quint32>( nif->get<quint32>( m, "Version" ), 2U );
	s << version;
	quint32	sf1 = nif->get<quint32>( m, "Shader Flags 1" );
	quint32	sf2 = nif->get<quint32>( m, "Shader Flags 2" );
	s << quint32( sf1 & 3U );	// tile flags
	Vector2	uvOffset = nif->get<Vector2>( m, "UV Offset" );
	Vector2	uvScale = nif->get<Vector2>( m, "UV Scale" );
	s << uvOffset[0];
	s << uvOffset[1];
	s << uvScale[0];
	s << uvScale[1];
	s << nif->get<float>( m, "Alpha" );
	s << quint8( bool( sf1 & 0x0004 ) );	// alpha blending
	s << nif->get<quint32>( m, "Alpha Source Blend Mode" );
	s << nif->get<quint32>( m, "Alpha Destination Blend Mode" );
	s << nif->get<quint8>( m, "Alpha Test Threshold" );
	s << quint8( bool( sf1 & 0x0008 ) );	// alpha testing
	s << quint8( bool( sf1 & 0x0010 ) );	// Z buffer write
	s << quint8( bool( sf1 & 0x0020 ) );	// Z buffer test
	s << quint8( bool( sf1 & 0x0040 ) );	// screen space reflections
	s << quint8( bool( sf1 & 0x0080 ) );	// SSR wetness control
	s << quint8( bool( sf1 & 0x0100 ) );	// decal
	s << quint8( bool( sf1 & 0x0200 ) );	// two sided
	s << quint8( bool( sf1 & 0x0400 ) );	// decal no fade
	s << quint8( bool( sf1 & 0x0800 ) );	// non-occluder
	s << quint8( bool( sf1 & 0x1000 ) );	// refraction
	s << quint8( bool( sf1 & 0x2000 ) );	// refraction falloff
	s << nif->get<float>( m, "Refraction Power" );
	s << quint8( bool( sf1 & 0x4000 ) );	// environment mapping
	if ( version < 10 )
		s << nif->get<float>( m, "Environment Map Scale" );
	s << quint8( bool( sf1 & 0x8000 ) );	// grayscale to palette mapping
	if ( version >= 6 )
		s << nif->get<quint8>( m, "Write Mask" );

	std::string	tmp;
	int	numTex = ( !isEffect ? ( version < 17 ? 9 : 10 ) : ( version < 10 ? 5 : ( version < 21 ? 8 : 10 ) ) );
	for ( int i = 0; i < numTex; i++ ) {
		tmp = nif->get<QString>( m, QString( "Texture %1" ).arg( i ) ).toStdString();
		s << tmp.c_str();
	}

	if ( !isEffect ) {
		s << quint8( bool( sf2 & 0x0001 ) );	// enable editor alpha ref
		if ( version < 8 ) {
			s << quint8( bool( sf2 & 0x02000000 ) );	// rim lighting
			s << nif->get<float>( m, "Rimlight Power" );
			s << nif->get<float>( m, "Backlight Power" );
			s << quint8( bool( sf2 & 0x04000000 ) );	// subsurface lighting
			s << nif->get<float>( m, "Subsurface Rolloff" );
		} else {
			s << quint8( bool( sf2 & 0x0002 ) );	// translucency
			s << quint8( bool( sf2 & 0x0004 ) );	// translucency thick object
			s << quint8( bool( sf2 & 0x0008 ) );	// translucency mix albedo with subsurface color
			Color3	c = nif->get<Color3>( m, "Translucency Subsurface Color" );
			s << c[0];
			s << c[1];
			s << c[2];
			s << nif->get<float>( m, "Translucency Transmissive Scale" );
			s << nif->get<float>( m, "Translucency Turbulence" );
		}
		s << quint8( bool( sf2 & 0x0010 ) );	// specular enabled
		Color3	c = nif->get<Color3>( m, "Specular Color" );
		s << c[0];
		s << c[1];
		s << c[2];
		s << nif->get<float>( m, "Specular Strength" );
		s << nif->get<float>( m, "Smoothness" );
		s << nif->get<float>( m, "Fresnel Power" );
		FloatVector8	v( -1.0f );
		const NifItem *	o = nif->getItem( m, "Wetness" );
		if ( o ) {
			v[0] = nif->get<float>( o, "Spec Scale" );
			v[1] = nif->get<float>( o, "Spec Power" );
			v[2] = nif->get<float>( o, "Min Var" );
			if ( version < 10 )
				v[3] = nif->get<float>( o, "Env Map Scale" );
			v[4] = nif->get<float>( o, "Fresnel Power" );
			v[5] = nif->get<float>( o, "Metalness" );
		}
		for ( size_t i = 0; i < 6; i++ ) {
			if ( i != 3 || version < 10 )
				s << v[i];
		}
		if ( version > 2 )
			s << quint8( bool( sf2 & 0x0020 ) );	// PBR
		if ( version >= 9 ) {
			s << quint8( bool( sf2 & 0x0040 ) );	// custom porosity
			s << nif->get<float>( m, "Porosity Value" );
		}
		tmp = nif->get<QString>( m, "Root Material" ).toStdString();
		s << tmp.c_str();
		s << quint8( bool( sf2 & 0x0080 ) );	// anisotropic lighting
		s << quint8( bool( sf2 & 0x0100 ) );	// emit enabled
		if ( sf2 & 0x0100 ) {
			c = nif->get<Color3>( m, "Emissive Color" );
			s << c[0];
			s << c[1];
			s << c[2];
		}
		s << nif->get<float>( m, "Emissive Multiple" );
		s << quint8( bool( sf2 & 0x0200 ) );	// model space normals
		s << quint8( bool( sf2 & 0x0400 ) );	// external emittance
		if ( version >= 12 ) {
			v = FloatVector8( 100.0f, 13.5f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f );
			o = nif->getItem( m, "Luminance" );
			if ( o ) {
				v[0] = nif->get<float>( o, "Lum Emittance" );
				if ( version >= 13 ) {
					v[1] = nif->get<float>( o, "Exposure Offset" );
					v[2] = nif->get<float>( o, "Final Exposure Min" );
					v[3] = nif->get<float>( o, "Final Exposure Max" );
				}
			}
			s << v[0];
			if ( version >= 13 ) {
				s << quint8( bool( sf2 & 0x0800 ) );	// use adaptive emissive
				for ( size_t i = 1; i < 4; i++ )
					s << v[i];
			}
		}
		if ( version < 8 )
			s << quint8( bool( sf2 & 0x08000000 ) );	// back lighting
		s << quint8( bool( sf2 & 0x1000 ) );	// receive shadows
		s << quint8( bool( sf2 & 0x2000 ) );	// hide secret
		s << quint8( bool( sf2 & 0x4000 ) );	// cast shadows
		s << quint8( bool( sf2 & 0x8000 ) );	// dissolve fade
		s << quint8( bool( sf2 & 0x00010000 ) );	// assume shadow mask
		s << quint8( bool( sf2 & 0x00020000 ) );	// glow map
		if ( version < 7 ) {
			s << quint8( bool( sf2 & 0x10000000 ) );	// window environment mapping
			s << quint8( bool( sf2 & 0x20000000 ) );	// eye environment mapping
		}
		s << quint8( bool( sf2 & 0x00040000 ) );	// hair
		c = nif->get<Color3>( m, "Hair Tint Color" );
		s << c[0];
		s << c[1];
		s << c[2];
		s << quint8( bool( sf2 & 0x00080000 ) );	// tree
		s << quint8( bool( sf2 & 0x00100000 ) );	// FaceGen
		s << quint8( bool( sf2 & 0x00200000 ) );	// skin tint
		s << quint8( bool( sf2 & 0x00400000 ) );	// tessellate
		if ( version < 3 ) {
			s << nif->get<float>( m, "Displacement Texture Bias" );
			s << nif->get<float>( m, "Displacement Texture Scale" );
			s << nif->get<float>( m, "Tessellation Pn Scale" );
			s << nif->get<float>( m, "Tessellation Base Factor" );
			s << nif->get<float>( m, "Tessellation Fade Distance" );
		}
		s << nif->get<float>( m, "Grayscale to Palette Scale" );
		s << quint8( bool( sf2 & 0x00800000 ) );	// skew specular alpha
		if ( version >= 3 ) {
			s << quint8( bool( sf2 & 0x01000000 ) );	// terrain
			if ( sf2 & 0x01000000 ) {
				if ( version == 3 )
					s << quint32( 0 );
				s << nif->get<float>( m, "Terrain Threshold Falloff" );
				s << nif->get<float>( m, "Terrain Tiling Distance" );
				s << nif->get<float>( m, "Terrain Rotation Angle" );
			}
		}
	} else {
		// effect material
		if ( version >= 10 ) {
			if ( version >= 21 ) {
				s << quint8( bool( sf2 & 0x0200 ) );	// glass enabled
				if ( sf2 & 0x0200 ) {
					Color3	c = nif->get<Color3>( m, "Glass Fresnel Color" );
					s << c[0];
					s << c[1];
					s << c[2];
					s << nif->get<float>( m, "Glass Refraction Scale" );
					s << nif->get<float>( m, "Glass Blur Scale Base" );
					if ( version >= 22 )
						s << nif->get<float>( m, "Glass Blur Scale Factor" );
				}
			}
			s << quint8( bool( sf2 & 0x0001 ) );	// environment mapping
			s << nif->get<float>( m, "Environment Map Scale" );
		}
		s << quint8( bool( sf2 & 0x0002 ) );	// blood enabled
		s << quint8( bool( sf2 & 0x0004 ) );	// effect lighting
		s << quint8( bool( sf2 & 0x0008 ) );	// use falloff
		s << quint8( bool( sf2 & 0x0010 ) );	// RGB falloff
		s << quint8( bool( sf2 & 0x0020 ) );	// grayscale to palette alpha
		s << quint8( bool( sf2 & 0x0040 ) );	// soft enabled
		Color3	c = nif->get<Color3>( m, "Base Color" );
		s << c[0];
		s << c[1];
		s << c[2];
		s << nif->get<float>( m, "Base Color Scale" );
		s << nif->get<float>( m, "Falloff Start Angle" );
		s << nif->get<float>( m, "Falloff Stop Angle" );
		s << nif->get<float>( m, "Falloff Start Opacity" );
		s << nif->get<float>( m, "Falloff Stop Opacity" );
		s << nif->get<float>( m, "Lighting Influence" );
		s << nif->get<quint8>( m, "Env Map Min LOD" );
		s << nif->get<float>( m, "Soft Falloff Depth" );
		if ( version >= 11 ) {
			c = nif->get<Color3>( m, "Emittance Color" );
			s << c[0];
			s << c[1];
			s << c[2];
			if ( version >= 15 ) {
				s << nif->get<float>( m, "Adaptive Emissive Exposure Offset" );
				s << nif->get<float>( m, "Adaptive Emissive Exposure Min" );
				s << nif->get<float>( m, "Adaptive Emissive Exposure Max" );
			}
			if ( version >= 16 )
				s << quint8( bool( sf2 & 0x0080 ) );	// glow map
			if ( version >= 20 )
				s << quint8( bool( sf2 & 0x0100 ) );	// effect PBR specular
		}
	}
}
