#version 120
#extension GL_ARB_shader_texture_lod : require

struct UVStream {
	vec4	scaleAndOffset;
	bool	useChannelTwo;
};

struct TextureSet {
	// >= 1: textureUnits index, -1: use replacement, 0: disabled
	int	textures[9];
	vec4	textureReplacements[9];
	float	floatParam;
};

struct Material {
	vec4	color;
	// bit 0 = color override mode (0: multiply, 1: lerp)
	// bit 1 = use vertex color as tint (1: yes)
	// bit 2 = is a flipbook
	int	flags;
	TextureSet	textureSet;
};

struct Layer {
	Material	material;
	UVStream	uvStream;
};

struct Blender {
	UVStream	uvStream;
	int	maskTexture;
	vec4	maskTextureReplacement;
	// 0 = "Linear" (default), 1 = "Additive", 2 = "PositionContrast",
	// 3 = "None", 4 = "CharacterCombine", 5 = "Skin"
	int	blendMode;
	// 0 = "Red" (default), 1 = "Green", 2 = "Blue", 3 = "Alpha"
	int	colorChannel;
	float	floatParams[5];
	bool	boolParams[8];
};

struct LayeredEmissivityComponent {
	bool	isEnabled;
	int	firstLayerIndex;
	vec4	firstLayerTint;
	int	firstLayerMaskIndex;
	int	secondLayerIndex;	// -1 if the layer is not active
	vec4	secondLayerTint;
	int	secondLayerMaskIndex;
	int	firstBlenderIndex;
	int	firstBlenderMode;
	int	thirdLayerIndex;	// -1 if the layer is not active
	vec4	thirdLayerTint;
	int	thirdLayerMaskIndex;
	int	secondBlenderIndex;
	int	secondBlenderMode;
	float	emissiveClipThreshold;
	bool	adaptiveEmittance;
	float	luminousEmittance;
	float	exposureOffset;
	bool	enableAdaptiveLimits;
	float	maxOffsetEmittance;
	float	minOffsetEmittance;
};

struct EmissiveSettingsComponent {
	bool	isEnabled;
	int	emissiveSourceLayer;
	vec4	emissiveTint;
	int	emissiveMaskSourceBlender;
	float	emissiveClipThreshold;
	bool	adaptiveEmittance;
	float	luminousEmittance;
	float	exposureOffset;
	bool	enableAdaptiveLimits;
	float	maxOffsetEmittance;
	float	minOffsetEmittance;
};

struct DecalSettingsComponent {
	bool	isDecal;
	float	materialOverallAlpha;
	int	writeMask;
	bool	isPlanet;
	bool	isProjected;
	bool	useParallaxOcclusionMapping;
	// >= 1: textureUnits index, <= 0: disabled
	int	surfaceHeightMap;
	float	parallaxOcclusionScale;
	bool	parallaxOcclusionShadows;
	int	maxParralaxOcclusionSteps;
	int	renderLayer;
	bool	useGBufferNormals;
	int	blendMode;
	bool	animatedDecalIgnoresTAA;
};

struct EffectSettingsComponent {
	// NOTE: the falloff settings here are deprecated and have been replaced by LayeredEdgeFalloffComponent
	bool	vertexColorBlend;
	bool	noHalfResOptimization;
	bool	softEffect;
	float	softFalloffDepth;
	bool	emissiveOnlyEffect;
	bool	emissiveOnlyAutomaticallyApplied;
	bool	receiveDirectionalShadows;
	bool	receiveNonDirectionalShadows;
	bool	isGlass;
	bool	frosting;
	float	frostingUnblurredBackgroundAlphaBlend;
	float	frostingBlurBias;
	float	materialOverallAlpha;
	bool	zTest;
	bool	zWrite;
	int	blendingMode;
	bool	backLightingEnable;
	float	backlightingScale;
	float	backlightingSharpness;
	float	backlightingTransparencyFactor;
	vec4	backLightingTintColor;
	bool	depthMVFixup;
	bool	depthMVFixupEdgesOnly;
	bool	forceRenderBeforeOIT;
	int	depthBiasInUlp;
};

struct LayeredEdgeFalloffComponent {
	float	falloffStartAngles[3];
	float	falloffStopAngles[3];
	float	falloffStartOpacities[3];
	float	falloffStopOpacities[3];
	// bits 0 to 2: active layers mask, bit 7: use RGB falloff
	int	flags;
};

struct OpacityComponent {	// for shader route = Effect
	int	firstLayerIndex;
	bool	secondLayerActive;
	int	secondLayerIndex;
	int	firstBlenderIndex;
	// 0 = "Lerp", 1 = "Additive", 2 = "Subtractive", 3 = "Multiplicative"
	int	firstBlenderMode;
	bool	thirdLayerActive;
	int	thirdLayerIndex;
	int	secondBlenderIndex;
	int	secondBlenderMode;
	float	specularOpacityOverride;
};

struct AlphaSettingsComponent {	// for shader route = Deferred
	bool	hasOpacity;
	float	alphaTestThreshold;
	int	opacitySourceLayer;
	// 0 = "Linear" (default), 1 = "Additive", 2 = "PositionContrast", 3 = "None"
	int	alphaBlenderMode;
	bool	useDetailBlendMask;
	bool	useVertexColor;
	int	vertexColorChannel;
	UVStream	opacityUVstream;
	float	heightBlendThreshold;
	float	heightBlendFactor;
	float	position;
	float	contrast;
	bool	useDitheredTransparency;
};

struct TranslucencySettingsComponent {
	bool	isEnabled;
	bool	isThin;
	bool	flipBackFaceNormalsInViewSpace;
	bool	useSSS;
	float	sssWidth;
	float	sssStrength;
	float	transmissiveScale;
	float	transmittanceWidth;
	float	specLobe0RoughnessScale;
	float	specLobe1RoughnessScale;
	int	transmittanceSourceLayer;
};

struct DetailBlenderSettings {
	bool	detailBlendMaskSupported;
	int	maskTexture;
	vec4	maskTextureReplacement;
	UVStream	uvStream;
};

struct LayeredMaterial {
	// shader model IDs are defined in lib/libfo76utils/src/mat_dump.cpp
	int	shaderModel;
	bool	isEffect;
	bool	hasOpacityComponent;
	int	numLayers;
	Layer	layers[6];
	Blender	blenders[5];
	LayeredEmissivityComponent	layeredEmissivity;
	EmissiveSettingsComponent	emissiveSettings;
	DecalSettingsComponent	decalSettings;
	EffectSettingsComponent	effectSettings;
	LayeredEdgeFalloffComponent	layeredEdgeFalloff;
	OpacityComponent	opacity;
	AlphaSettingsComponent	alphaSettings;
	TranslucencySettingsComponent	translucencySettings;
	DetailBlenderSettings	detailBlender;
};

uniform samplerCube	CubeMap;
uniform samplerCube	CubeMap2;
uniform bool	hasCubeMap;
uniform bool	hasSpecular;

uniform sampler2D	textureUnits[SF_NUM_TEXTURE_UNITS];

uniform vec4 parallaxOcclusionSettings;	// min. steps, max. steps, height scale, height offset
// bit 0: alpha testing, bit 1: alpha blending
uniform int alphaFlags;

uniform	LayeredMaterial	lm;

varying vec3 LightDir;
varying vec3 ViewDir;

varying vec4 A;
varying vec4 C;
varying vec4 D;

varying mat3 btnMatrix;
varying mat4 reflMatrix;

vec3 ViewDir_norm = normalize( ViewDir );
mat3 btnMatrix_norm = mat3( normalize( btnMatrix[0] ), normalize( btnMatrix[1] ), normalize( btnMatrix[2] ) );

#ifndef M_PI
	#define M_PI 3.1415926535897932384626433832795
#endif

#define FLT_EPSILON 1.192092896e-07F // smallest such that 1.0 + FLT_EPSILON != 1.0

float emissiveIntensity( bool useAdaptive, bool adaptiveLimits, vec4 luminanceParams )
{
	float	l = luminanceParams[0];	// luminousEmittance

	if ( useAdaptive ) {
		l = dot( A.rgb * 20.0 + D.rgb * 80.0, vec3(0.2126, 0.7152, 0.0722) );
		l = l * exp2( luminanceParams[1] );	// exposureOffset
		if ( adaptiveLimits )	// maxOffsetEmittance, minOffsetEmittance
			l = clamp( l, luminanceParams[3], luminanceParams[2] );
	}

	return sqrt( l * 0.01 );
}

float LightingFuncGGX_REF( float LdotR, float NdotL, float NdotV, float roughness )
{
	float alpha = roughness * roughness;
	// D (GGX normal distribution)
	float alphaSqr = alpha * alpha;
	// denom = NdotH * NdotH * (alphaSqr - 1.0) + 1.0,
	// LdotR = NdotH * NdotH * 2.0 - 1.0
	float denom = LdotR * alphaSqr + alphaSqr + (1.0 - LdotR);
	float D = alphaSqr / (denom * denom);
	// no pi because BRDF -> lighting
	// G (remapped hotness, see Unreal Shading)
	float	k = ( alpha + 2.0 * roughness + 1.0 ) / 8.0;
	float	G = NdotL / ( mix(NdotL, 1.0, k) * mix(NdotV, 1.0, k) );

	return D * G;
}

vec3 tonemap(vec3 x, float y)
{
	float a = 0.15;
	float b = 0.50;
	float c = 0.10;
	float d = 0.20;
	float e = 0.02;
	float f = 0.30;

	vec3 z = x * (y * 4.22978723);
	z = (z * (a * z + b * c) + d * e) / (z * (a * z + b) + d * f) - e / f;
	return z / (y * 0.93333333);
}

vec2 getTexCoord(in UVStream uvStream)
{
	vec2	offset = ( !uvStream.useChannelTwo ? gl_TexCoord[0].st : gl_TexCoord[0].pq );
	return offset * uvStream.scaleAndOffset.xy + uvStream.scaleAndOffset.zw;
}

vec4 getLayerTexture(int layerNum, int textureNum, vec2 offset)
{
	int	n = lm.layers[layerNum].material.textureSet.textures[textureNum];
	if ( n < 0 )
		return lm.layers[layerNum].material.textureSet.textureReplacements[textureNum];
	return texture2D(textureUnits[n], offset);
}

float getDetailBlendMask()
{
	if ( !( lm.detailBlender.detailBlendMaskSupported && lm.detailBlender.maskTexture != 0 ) )
		return 1.0;
	if ( lm.detailBlender.maskTexture < 0 )
		return lm.detailBlender.maskTextureReplacement.r;
	return texture2D( textureUnits[lm.detailBlender.maskTexture], getTexCoord( lm.detailBlender.uvStream ) ).r;
}

float getBlenderMask(int n)
{
	float	r = 1.0;
	if ( lm.blenders[n].maskTexture != 0 ) {
		if ( lm.blenders[n].maskTexture < 0 ) {
			r = lm.blenders[n].maskTextureReplacement.r;
		} else {
			vec2	offset = getTexCoord(lm.blenders[n].uvStream);
			r = texture2D(textureUnits[lm.blenders[n].maskTexture], offset).r;
		}
	}
	if ( lm.blenders[n].boolParams[5] )
		r *= C[lm.blenders[n].colorChannel];
	if ( lm.blenders[n].boolParams[7] )
		r *= getDetailBlendMask();
	return r * lm.blenders[n].floatParams[4];	// mask intensity
}

// parallax occlusion mapping based on code from
// https://web.archive.org/web/20150419215321/http://sunandblackcat.com/tipFullView.php?l=eng&topicid=28

vec2 parallaxMapping( int n, vec3 V, vec2 offset )
{
	if ( parallaxOcclusionSettings.z < 0.0005 )
		return offset;	// disabled

	// determine optimal height of each layer
	float	layerHeight = 1.0 / mix( parallaxOcclusionSettings.y, parallaxOcclusionSettings.x, abs(V.z) );

	// current height of the layer
	float	curLayerHeight = 1.0;
	vec2	dtex = parallaxOcclusionSettings.z * V.xy / max( abs(V.z), 0.02 );
	// current texture coordinates
	vec2	currentTextureCoords = offset + ( dtex * parallaxOcclusionSettings.w );
	// shift of texture coordinates for each layer
	dtex *= layerHeight;

	// height from heightmap
	float	heightFromTexture = texture2D( textureUnits[n], currentTextureCoords ).r;

	// while point is above the surface
	while ( curLayerHeight > heightFromTexture ) {
		// to the next layer
		curLayerHeight -= layerHeight;
		// shift of texture coordinates
		currentTextureCoords -= dtex;
		// new height from heightmap
		heightFromTexture = texture2D( textureUnits[n], currentTextureCoords ).r;
	}

	// previous texture coordinates
	vec2	prevTCoords = currentTextureCoords + dtex;

	// heights for linear interpolation
	float	nextH = curLayerHeight - heightFromTexture;
	float	prevH = curLayerHeight + layerHeight - texture2D( textureUnits[n], prevTCoords ).r;

	// proportions for linear interpolation
	float	weight = nextH / ( nextH - prevH );

	// return interpolation of texture coordinates
	return mix( currentTextureCoords, prevTCoords, weight );
}

void main()
{
	if ( lm.shaderModel == 45 )	// "Invisible"
		discard;

	vec3	baseMap = C.rgb;
	vec3	normal = vec3(0.0, 0.0, 1.0);
	vec3	pbrMap = vec3(0.0, 0.0, 1.0);	// roughness, metalness, AO
	float	baseAlpha = C.a;
	float	alpha = 1.0;
	vec3	emissive = vec3(0.0);
	vec3	transmissive = vec3(0.0);
	float	falloffLayerMask = float(lm.layeredEdgeFalloff.flags);
	int	numLayers = min( lm.numLayers, 6 );

	for ( int i = 0; i < numLayers; i++ ) {
		vec3	layerBaseMap = vec3(0.0);
		vec3	layerNormal = vec3(0.0, 0.0, 1.0);
		vec3	layerPBRMap = vec3(0.0, 0.0, 1.0);

		int	blendMode = 3;	// "None"
		if ( i > 0 )
			blendMode = lm.blenders[i - 1].blendMode;

		vec2	offset = getTexCoord( lm.layers[i].uvStream );
		// _height.dds
		if ( lm.layers[i].material.textureSet.textures[6] >= 1 )
			offset = parallaxMapping( lm.layers[i].material.textureSet.textures[6], normalize( ViewDir_norm * btnMatrix_norm ), offset );
		// _color.dds
		if ( lm.layers[i].material.textureSet.textures[0] != 0 )
			layerBaseMap = getLayerTexture(i, 0, offset).rgb;
		{
			vec4	tintColor = ( fract( float(lm.layers[i].material.flags) * 0.25 ) < 0.499 ? lm.layers[i].material.color : C );
			if ( fract( float(lm.layers[i].material.flags) * 0.5 ) < 0.499 )
				layerBaseMap *= tintColor.rgb;
			else
				layerBaseMap = mix( layerBaseMap, tintColor.rgb, tintColor.a );
		}
		// _normal.dds
		if ( lm.layers[i].material.textureSet.textures[1] != 0 ) {
			layerNormal.rg = getLayerTexture(i, 1, offset).rg * lm.layers[i].material.textureSet.floatParam;
			// Calculate missing blue channel
			layerNormal.b = sqrt(max(1.0 - dot(layerNormal.rg, layerNormal.rg), 0.0));
		}
		// _rough.dds
		if ( lm.layers[i].material.textureSet.textures[3] != 0 )
			layerPBRMap.r = getLayerTexture(i, 3, offset).r;
		// _metal.dds
		if ( lm.layers[i].material.textureSet.textures[4] != 0 )
			layerPBRMap.g = getLayerTexture(i, 4, offset).r;
		// _ao.dds
		if ( lm.layers[i].material.textureSet.textures[5] != 0 )
			layerPBRMap.b = getLayerTexture(i, 5, offset).r;

		// falloff
		float	f = 1.0;
		falloffLayerMask *= 0.5;
		if ( fract( falloffLayerMask ) > 0.499 ) {
			float	startAngle = cos( radians(lm.layeredEdgeFalloff.falloffStartAngles[i]) );
			float	stopAngle = cos( radians(lm.layeredEdgeFalloff.falloffStopAngles[i]) );
			float	startOpacity = lm.layeredEdgeFalloff.falloffStartOpacities[i];
			float	stopOpacity = lm.layeredEdgeFalloff.falloffStopOpacities[i];
			float	NdotV = abs( dot(btnMatrix_norm[2], ViewDir_norm) );
			f = 0.5;
			if ( stopAngle > (startAngle + 0.000001) )
				f = smoothstep( startAngle, stopAngle, NdotV );
			else if ( startAngle > (stopAngle + 0.000001) )
				f = 1.0 - smoothstep( stopAngle, startAngle, NdotV );
			f = clamp( mix(startOpacity, stopOpacity, f), 0.0, 1.0 );
			if ( lm.layeredEdgeFalloff.flags >= 128 )
				layerBaseMap *= f;
		}

		// material layering
		float	layerMask = 1.0;
		if ( i == 0 ) {
			if ( lm.decalSettings.isDecal && lm.layers[0].material.textureSet.textures[0] == 0 )
				discard;
			baseMap = layerBaseMap;
			normal = layerNormal;
			pbrMap = layerPBRMap;
			alpha = f;
		} else {
			layerMask = getBlenderMask( i - 1 );
			if ( blendMode != 3 && !( lm.isEffect && lm.effectSettings.isGlass ) ) {
				// TODO: correctly implement Skin, instead of interpreting it as Linear
				float	srcMask = layerMask;
				if ( blendMode == 2 ) {
					// PositionContrast
					float	blendPosition = lm.blenders[i - 1].floatParams[2];
					float	blendContrast = lm.blenders[i - 1].floatParams[3];
					blendContrast = max( blendContrast * min(blendPosition, 1.0 - blendPosition), 0.001 );
					blendPosition = ( blendPosition - 0.5 ) * 3.17;
					blendPosition = ( blendPosition * blendPosition + 1.0 ) * blendPosition + 0.5;
					float	maskMin = blendPosition - blendContrast;
					float	maskMax = blendPosition + blendContrast;
					srcMask = ( srcMask - maskMin ) / ( maskMax - maskMin );
				} else if ( blendMode == 4 ) {
					// CharacterCombine: blend color, roughness and metalness multiplicatively
					layerBaseMap = layerBaseMap * baseMap * 2.0;
					layerPBRMap.r = layerPBRMap.r * pbrMap.r * 2.0;
					if ( layerPBRMap.g < 0.5 )
						layerPBRMap.g = layerPBRMap.g * pbrMap.g;
					else
						layerPBRMap.g = layerPBRMap.g + pbrMap.g - layerPBRMap.g * pbrMap.g;
				} else if ( blendMode == 1 ) {
					// Additive
					layerBaseMap += baseMap;
					layerPBRMap += pbrMap;
					if ( !lm.blenders[i - 1].boolParams[4] )
						layerNormal += normal;
				}
				srcMask = clamp( srcMask, 0.0, 1.0 ) * f;
				if ( lm.blenders[i - 1].boolParams[0] )
					baseMap = min( mix( baseMap, layerBaseMap, srcMask ), vec3(1.0) );	// blend color
				layerPBRMap = min( mix( pbrMap, layerPBRMap, srcMask ), vec3(1.0) );
				if ( lm.blenders[i - 1].boolParams[1] )
					pbrMap.g = layerPBRMap.g;	// blend metalness
				if ( lm.blenders[i - 1].boolParams[2] )
					pbrMap.r = layerPBRMap.r;	// blend roughness
				if ( lm.blenders[i - 1].boolParams[3] ) {
					if ( lm.blenders[i - 1].boolParams[4] ) {
						normal.rg = normal.rg + ( layerNormal.rg * srcMask );	// blend normals additively
						normal.b = sqrt( max( 1.0 - dot(normal.rg, normal.rg), 0.0 ) );
					} else {
						normal = normalize( mix( normal, layerNormal, srcMask ) );
					}
				}
				if ( lm.blenders[i - 1].boolParams[6] )
					pbrMap.b = layerPBRMap.b;	// blend ambient occlusion
			}
		}

		if ( lm.layers[i].material.textureSet.textures[2] != 0 ) {
			// _opacity.dds
			if ( lm.isEffect ) {
				float	a = getLayerTexture( i, 2, offset ).r;
				if ( lm.hasOpacityComponent ) {
					int	opacityBlendMode = -1;
					if ( i == lm.opacity.firstLayerIndex ) {
						baseAlpha = a;
					} else if ( !lm.effectSettings.isGlass ) {
						// FIXME: this assumes blender index = layer index - 1
						if ( lm.opacity.secondLayerActive && i == lm.opacity.secondLayerIndex )
							opacityBlendMode = lm.opacity.firstBlenderMode;
						else if ( lm.opacity.thirdLayerActive && i == lm.opacity.thirdLayerIndex )
							opacityBlendMode = lm.opacity.secondBlenderMode;
					}
					if ( opacityBlendMode == 0 )
						baseAlpha = mix( baseAlpha, a, layerMask );
					else if ( opacityBlendMode == 1 )
						baseAlpha = min( baseAlpha + a * layerMask, 1.0 );
					else if ( opacityBlendMode == 2 )
						baseAlpha = max( baseAlpha - a * layerMask, 0.0 );
					else if ( opacityBlendMode == 3 )
						baseAlpha *= a * layerMask;
				} else if ( i == 0 ) {
					baseAlpha = a;
				}
			} else if ( lm.alphaSettings.hasOpacity && i == lm.alphaSettings.opacitySourceLayer ) {
				if ( lm.layers[i].material.flags < 4 )
					baseAlpha = getLayerTexture( i, 2, getTexCoord(lm.alphaSettings.opacityUVstream) ).r;
				else
					baseAlpha = getLayerTexture( i, 2, offset ).r;
			}
		}

		if ( lm.layers[i].material.textureSet.textures[7] != 0 ) {
			// _emissive.dds
			// TODO: layered emissivity masks
			vec4	tmp = vec4(0.0);
			if ( lm.emissiveSettings.isEnabled && i == lm.emissiveSettings.emissiveSourceLayer )
				tmp = lm.emissiveSettings.emissiveTint;
			else if ( lm.layeredEmissivity.isEnabled && i == lm.layeredEmissivity.firstLayerIndex )
				tmp = lm.layeredEmissivity.firstLayerTint;
			else if ( lm.layeredEmissivity.isEnabled && i == lm.layeredEmissivity.secondLayerIndex )
				tmp = lm.layeredEmissivity.secondLayerTint;
			else if ( lm.layeredEmissivity.isEnabled && i == lm.layeredEmissivity.thirdLayerIndex )
				tmp = lm.layeredEmissivity.thirdLayerTint;
			else
				continue;
			emissive += getLayerTexture( i, 7, offset ).rgb * tmp.rgb * tmp.a;
		}

		if ( lm.layers[i].material.textureSet.textures[8] != 0 ) {
			// _transmissive.dds
			if ( lm.translucencySettings.isEnabled && i == lm.translucencySettings.transmittanceSourceLayer )
				transmissive = vec3( getLayerTexture( i, 8, offset ).r * lm.translucencySettings.transmissiveScale );
		}
	}

	vec4	color = vec4(1.0);

	if ( alphaFlags != 0 ) {
		if ( lm.isEffect ) {
			if ( lm.effectSettings.vertexColorBlend ) {
				baseMap *= C.rgb;
				baseAlpha *= C.a;
			}
			alpha = alpha * lm.effectSettings.materialOverallAlpha * baseAlpha;
			// alpha test settings seem to be ignored for effects, and a fixed threshold of 1/128 is used instead
			if ( !( alpha > 0.0078 ) )
				discard;
			if ( lm.effectSettings.blendingMode == 2 )	// SourceSoftAdditive
				baseMap *= alpha;
		} else {
			if ( lm.decalSettings.isDecal )
				alpha = lm.decalSettings.materialOverallAlpha;
			if ( lm.alphaSettings.hasOpacity ) {
				if ( lm.alphaSettings.useDetailBlendMask )
					alpha *= getDetailBlendMask();
				if ( lm.alphaSettings.useVertexColor )
					alpha *= C[lm.alphaSettings.vertexColorChannel];
			}
			alpha = alpha * baseAlpha;
			if ( ( alphaFlags == 1 || alphaFlags == 3 ) && !( alpha > lm.alphaSettings.alphaTestThreshold ) )
				discard;
		}
		if ( alphaFlags >= 2 )
			color.a = alpha;
	}

	normal = normalize( btnMatrix_norm * normal );
	if ( !gl_FrontFacing )
		normal *= -1.0;

	vec3	L = normalize(LightDir);
	vec3	V = ViewDir_norm;
	vec3	R = reflect(-V, normal);

	float	NdotL = dot(normal, L);
	float	NdotL0 = max(NdotL, 0.0);
	float	LdotR = dot(L, R);
	float	NdotV = abs(dot(normal, V));
	float	LdotV = dot(L, V);

	vec3	reflectedWS = vec3(reflMatrix * (gl_ModelViewMatrixInverse * vec4(R, 0.0)));
	vec3	normalWS = vec3(reflMatrix * (gl_ModelViewMatrixInverse * vec4(normal, 0.0)));

	vec3	f0 = mix(vec3(0.04), baseMap, pbrMap.g);
	vec3	albedo = baseMap * (1.0 - pbrMap.g);

	// Specular
	float	roughness = pbrMap.r;
	vec3	spec = D.rgb * LightingFuncGGX_REF( LdotR, NdotL0, NdotV, max(roughness, 0.02) );

	// Diffuse
	vec3	diffuse = vec3(NdotL0);
	// Fresnel
	float	LdotH = sqrt( max(LdotV * 0.5 + 0.5, 0.0) );
	vec2	fDirect = texture2DLod(textureUnits[0], vec2(LdotH, NdotL0), 0.0).ba;
	spec *= mix(f0, vec3(1.0), fDirect.x);
	vec4	envLUT = texture2DLod(textureUnits[0], vec2(NdotV, roughness), 0.0);
	vec2	fDiff = vec2(fDirect.y, envLUT.b);
	fDiff = fDiff * (LdotH * LdotH * roughness * 2.0 - 0.5) + 1.0;
	diffuse *= (vec3(1.0) - f0) * fDiff.x * fDiff.y;

	// Environment
	vec3	refl = vec3(0.0);
	vec3	ambient = A.rgb;
	if ( hasCubeMap ) {
		float	m = roughness * (roughness * -4.0 + 10.0);
		refl = textureCubeLod(CubeMap, reflectedWS, max(m, 0.0)).rgb;
		refl *= ambient;
		ambient *= textureCubeLod(CubeMap2, normalWS, 0.0).rgb;
	} else {
		ambient *= 0.08;
		refl = ambient;
	}
	vec3	f = mix(f0, vec3(1.0), envLUT.r);
	if (!hasSpecular) {
		albedo = baseMap;
		diffuse = vec3(NdotL0);
		spec = vec3(0.0);
		f = vec3(0.0);
	} else {
		float	fDiffEnv = envLUT.b * ((NdotV + 1.0) * roughness - 0.5) + 1.0;
		ambient *= (vec3(1.0) - f0) * fDiffEnv;
	}
	float	ao = pbrMap.b;
	refl *= f * envLUT.g;

	// Diffuse
	color.rgb = diffuse * albedo * D.rgb;
	// Ambient
	color.rgb += ambient * albedo;
	// Specular
	color.rgb += spec;
	color.rgb += refl;
	color.rgb *= ao;

	// Emissive
	if ( lm.emissiveSettings.isEnabled ) {
		emissive *= emissiveIntensity( lm.emissiveSettings.adaptiveEmittance, lm.emissiveSettings.enableAdaptiveLimits, vec4(lm.emissiveSettings.luminousEmittance, lm.emissiveSettings.exposureOffset, lm.emissiveSettings.maxOffsetEmittance, lm.emissiveSettings.minOffsetEmittance) );
	} else if ( lm.layeredEmissivity.isEnabled ) {
		emissive *= emissiveIntensity( lm.layeredEmissivity.adaptiveEmittance, lm.layeredEmissivity.enableAdaptiveLimits, vec4(lm.layeredEmissivity.luminousEmittance, lm.layeredEmissivity.exposureOffset, lm.layeredEmissivity.maxOffsetEmittance, lm.layeredEmissivity.minOffsetEmittance) );
	}
	color.rgb += emissive;

	// Transmissive
	if ( lm.translucencySettings.isEnabled && lm.translucencySettings.isThin ) {
		transmissive *= albedo * ( vec3(1.0) - f ) * ao;
		// TODO: implement flipBackFaceNormalsInViewSpace
		color.rgb += transmissive * D.rgb * max( -NdotL, 0.0 );
		if ( hasCubeMap )
			color.rgb += textureCubeLod( CubeMap2, -normalWS, 0.0 ).rgb * transmissive * A.rgb;
		else
			color.rgb += transmissive * A.rgb * 0.08;
	}

	color.rgb = tonemap(color.rgb * D.a, A.a);

	gl_FragColor = color;
}
