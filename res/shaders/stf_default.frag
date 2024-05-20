#version 400 compatibility
#extension GL_ARB_shader_texture_lod : require

struct UVStream {
	vec2	scale;
	vec2	offset;
	bool	useChannelTwo;
};

struct TextureSet {
	// >= 1: textureUnits index, -1: use replacement, 0: disabled
	int	textures[11];
	vec4	textureReplacements[11];
	float	floatParam;
};

struct Material {
	vec4	color;
	// bit 0 = color override mode (0: multiply, 1: lerp)
	// bit 1 = use vertex color as tint (1: yes)
	// bits 2 to 8 = flipbook columns (0: not a flipbook)
	// bits 9 to 15 = flipbook rows (0: not a flipbook)
	// bits 16 to 29 = flipbook frame
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

struct TerrainTintSettingsComponent {
	bool	isEnabled;
	float	terrainBlendStrength;
	float	terrainBlendGradientFactor;
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
	bool	useFallOff;
	bool	useRGBFallOff;
	float	falloffStartAngle;
	float	falloffStopAngle;
	float	falloffStartOpacity;
	float	falloffStopOpacity;
	bool	vertexColorBlend;
	bool	isAlphaTested;
	float	alphaTestThreshold;
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

struct TerrainSettingsComponent {
	bool	isEnabled;
	int	textureMappingType;
	float	rotationAngle;
	float	blendSoftness;
	float	tilingDistance;
	float	maxDisplacement;
	float	displacementMidpoint;
};

struct LayeredMaterial {
	// shader model IDs are defined in lib/libfo76utils/src/mat_dump.cpp
	int	shaderModel;
	bool	isEffect;
	bool	isTwoSided;
	bool	hasOpacityComponent;
	bool	layersEnabled[4];
	Layer	layers[4];
	Blender	blenders[3];
	LayeredEmissivityComponent	layeredEmissivity;
	EmissiveSettingsComponent	emissiveSettings;
	DecalSettingsComponent	decalSettings;
	EffectSettingsComponent	effectSettings;
	OpacityComponent	opacity;
	AlphaSettingsComponent	alphaSettings;
	TranslucencySettingsComponent	translucencySettings;
	TerrainSettingsComponent	terrainSettings;
};

uniform samplerCube	CubeMap;
uniform samplerCube	CubeMap2;
uniform bool	hasCubeMap;
uniform bool	hasSpecular;

uniform sampler2D	textureUnits[SF_NUM_TEXTURE_UNITS];

uniform vec4 solidColor;

uniform bool isWireframe;
uniform bool isSkinned;
uniform mat4 worldMatrix;
uniform vec2 parallaxOcclusionSettings;	// max. steps, height scale

uniform	LayeredMaterial	lm;

in vec3 LightDir;
in vec3 ViewDir;

in vec4 A;
in vec4 C;
in vec4 D;

in mat3 btnMatrix;
in mat4 reflMatrix;

out vec4 fragColor;

vec3 ViewDir_norm = normalize( ViewDir );
mat3 btnMatrix_norm = mat3( normalize( btnMatrix[0] ), normalize( btnMatrix[1] ), normalize( btnMatrix[2] ) );

#ifndef M_PI
	#define M_PI 3.1415926535897932384626433832795
#endif

#define FLT_EPSILON 1.192092896e-07F // smallest such that 1.0 + FLT_EPSILON != 1.0

vec3 LightingFuncGGX_REF(float NdotL, float LdotR, float NdotV, float roughness)
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
	float	k = (alpha + 2 * roughness + 1) / 8.0;
	float	G = NdotL / (mix(NdotL, 1, k) * mix(NdotV, 1, k));

	return vec3(D * G);
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
	vec2	offset;
	if ( uvStream.useChannelTwo )
		offset = gl_TexCoord[0].pq;	// this may be incorrect
	else
		offset = gl_TexCoord[0].st;
	return offset * uvStream.scale + uvStream.offset;
}

vec4 getLayerTexture(int layerNum, int textureNum, vec2 offset)
{
	int	n = lm.layers[layerNum].material.textureSet.textures[textureNum];
	if ( n < 1 )
		return lm.layers[layerNum].material.textureSet.textureReplacements[textureNum];
	return texture(textureUnits[n], offset);
}

float getBlenderMask(int n)
{
	float	r = 1.0;
	if ( lm.blenders[n].maskTexture != 0 ) {
		if ( lm.blenders[n].maskTexture < 0 ) {
			r = lm.blenders[n].maskTextureReplacement.r;
		} else {
			vec2	offset = getTexCoord(lm.blenders[n].uvStream);
			r = texture(textureUnits[lm.blenders[n].maskTexture], offset).r;
		}
	}
	if ( lm.blenders[n].colorChannel >= 0 )
		r *= C[lm.blenders[n].colorChannel];
	return r;
}

// parallax occlusion mapping based on code from
// https://web.archive.org/web/20150419215321/http://sunandblackcat.com/tipFullView.php?l=eng&topicid=28

vec2 parallaxMapping(int n, vec3 V, vec2 offset, float parallaxScale, float maxLayers)
{
	// determine optimal height of each layer
	float	layerHeight = 1.0 / mix( maxLayers, 8.0, abs(V.z) );

	// current height of the layer
	float	curLayerHeight = 1.0;
	// shift of texture coordinates for each layer
	vec2	dtex = parallaxScale * V.xy * layerHeight / max( abs(V.z), 0.02 );

	// current texture coordinates
	vec2	currentTextureCoords = offset;

	// height from heightmap
	float	heightFromTexture = texture( textureUnits[n], currentTextureCoords ).r;

	// while point is above the surface
	while ( curLayerHeight > heightFromTexture ) {
		// to the next layer
		curLayerHeight -= layerHeight;
		// shift of texture coordinates
		currentTextureCoords -= dtex;
		// new height from heightmap
		heightFromTexture = texture( textureUnits[n], currentTextureCoords ).r;
	}

	// previous texture coordinates
	vec2	prevTCoords = currentTextureCoords + dtex;

	// heights for linear interpolation
	float	nextH = curLayerHeight - heightFromTexture;
	float	prevH = curLayerHeight + layerHeight - texture( textureUnits[n], prevTCoords ).r;

	// proportions for linear interpolation
	float	weight = nextH / ( nextH - prevH );

	// return interpolation of texture coordinates
	return mix( currentTextureCoords, prevTCoords, weight );
}

void getLayer(int n, vec2 offset, inout vec4 baseMap, inout vec3 normalMap, inout vec3 pbrMap)
{
	// _height.dds
	if ( lm.layers[n].material.textureSet.textures[6] >= 1 )
		offset = parallaxMapping( lm.layers[n].material.textureSet.textures[6], normalize( ViewDir_norm * btnMatrix_norm ), offset, parallaxOcclusionSettings.y, parallaxOcclusionSettings.x );
	// _color.dds
	if ( lm.layers[n].material.textureSet.textures[0] != 0 )
		baseMap.rgb = getLayerTexture(n, 0, offset).rgb;
	if ( n == 0 || lm.layers[n].material.textureSet.textures[0] != 0 ) {
		baseMap.rgb *= lm.layers[n].material.color.rgb;
		if ( (lm.layers[n].material.flags & 2) != 0 )
			baseMap.rgb *= C.rgb;
	}
	// _normal.dds
	if ( lm.layers[n].material.textureSet.textures[1] != 0 ) {
		normalMap.rg = getLayerTexture(n, 1, offset).rg;
		// Calculate missing blue channel
		normalMap.b = sqrt(max(1.0 - dot(normalMap.rg, normalMap.rg), 0.0));
	}
	// _rough.dds
	if ( lm.layers[n].material.textureSet.textures[3] != 0 )
		pbrMap.r = getLayerTexture(n, 3, offset).r;
	// _metal.dds
	if ( lm.layers[n].material.textureSet.textures[4] != 0 )
		pbrMap.g = getLayerTexture(n, 4, offset).r;
	// _ao.dds
	if ( lm.layers[n].material.textureSet.textures[5] != 0 )
		pbrMap.b = getLayerTexture(n, 5, offset).r;
}

void main(void)
{
	if ( isWireframe ) {
		fragColor = solidColor;
		return;
	}
	if ( lm.shaderModel == 45 )	// "Invisible"
		discard;

	vec4	baseMap = vec4(1.0);
	vec3	normal = vec3(0.0, 0.0, 1.0);
	vec3	pbrMap = vec3(0.75, 0.0, 1.0);	// roughness, metalness, AO
	float	alpha = 1.0;
	vec3	emissive = vec3(0.0);

	for (int i = 0; i < 4; i++) {
		if ( !lm.layersEnabled[i] )
			break;

		vec2	offset = getTexCoord( lm.layers[i].uvStream );
		if ( (lm.layers[i].material.flags & 0xFFFC) != 0 ) {
			// flipbook
			int	w = ( lm.layers[i].material.flags >> 2 ) & 0x7F;
			int	h = ( lm.layers[i].material.flags >> 9 ) & 0x7F;
			int	n = lm.layers[i].material.flags >> 16;
			offset = ( fract( offset ) + vec2( float(n % w), float(n / w) ) ) / vec2( float(w), float(h) );
		}

		if ( i == 0 ) {
			if ( lm.decalSettings.isDecal && lm.layers[0].material.textureSet.textures[0] == 0 )
				discard;
			getLayer( 0, offset, baseMap, normal, pbrMap );
		} else {
			vec4	layerBaseMap = baseMap;
			vec3	layerNormal = normal;
			vec3	layerPBRMap = pbrMap;
			getLayer( i, offset, layerBaseMap, layerNormal, layerPBRMap );

			float	layerMask = getBlenderMask(i - 1);
			switch ( lm.blenders[i - 1].blendMode) {
				case 0:		// Linear
					baseMap.rgb = mix(baseMap.rgb, layerBaseMap.rgb, layerMask);
					normal = normalize(mix(normal, layerNormal, layerMask));
					pbrMap = mix(pbrMap, layerPBRMap, layerMask);
					break;
				case 1:		// Additive (TODO)
					break;
				case 2:		// PositionContrast (TODO)
					break;
			}
		}

		if ( lm.layers[i].material.textureSet.textures[2] != 0 ) {
			// _opacity.dds
			if ( lm.isEffect && lm.hasOpacityComponent && i == lm.opacity.firstLayerIndex ) {
				baseMap.a *= getLayerTexture( i, 2, offset ).r;
			} else if ( lm.alphaSettings.hasOpacity && i == lm.alphaSettings.opacitySourceLayer ) {
				if ( (lm.layers[i].material.flags & 0xFFFC) == 0 )
					baseMap.a *= getLayerTexture( i, 2, getTexCoord(lm.alphaSettings.opacityUVstream) ).r;
				else
					baseMap.a *= getLayerTexture( i, 2, offset ).r;
				alpha = lm.layers[i].material.color.a;
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
	}

	normal = normalize( btnMatrix_norm * normal );
	if ( !gl_FrontFacing && lm.isTwoSided )
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

	if ( lm.alphaSettings.hasOpacity && lm.alphaSettings.useVertexColor )
		alpha *= C[lm.alphaSettings.vertexColorChannel];

	if ( lm.isEffect ) {
		if ( lm.effectSettings.useFallOff || lm.effectSettings.useRGBFallOff ) {
			float	startAngle = cos(radians(lm.effectSettings.falloffStartAngle));
			float	stopAngle = cos(radians(lm.effectSettings.falloffStopAngle));
			float	startOpacity = lm.effectSettings.falloffStartOpacity;
			float	stopOpacity = lm.effectSettings.falloffStopOpacity;
			float	f = 0.5;
			if ( stopAngle > (startAngle + 0.000001) )
				f = smoothstep(startAngle, stopAngle, NdotV);
			else if ( startAngle > (stopAngle + 0.000001) )
				f = 1.0 - smoothstep(stopAngle, startAngle, NdotV);
			f = clamp(mix(startOpacity, stopOpacity, f), 0.0, 1.0);
			if ( lm.effectSettings.useRGBFallOff )
				baseMap.rgb *= f;
			if ( lm.effectSettings.useFallOff )
				baseMap.a *= f;
		}
		if ( lm.effectSettings.vertexColorBlend )
			baseMap *= C;
		alpha = lm.effectSettings.materialOverallAlpha;
	}

	if ( lm.decalSettings.isDecal )
		alpha = lm.decalSettings.materialOverallAlpha;

	vec4	color;
	vec3	albedo = baseMap.rgb;

	// emissive intensity (FIXME: this is probably incorrect)
	if ( lm.emissiveSettings.isEnabled ) {
		emissive *= exp2( lm.emissiveSettings.exposureOffset * 0.5 );
	} else if ( lm.layeredEmissivity.isEnabled ) {
		emissive *= exp2( lm.layeredEmissivity.exposureOffset * 0.5 );
	}

	vec3	f0 = mix(vec3(0.04), albedo, pbrMap.g);
	albedo = albedo * (1.0 - pbrMap.g);

	// Specular
	float	roughness = pbrMap.r;
	vec3	spec = LightingFuncGGX_REF(NdotL0, LdotR, NdotV, max(roughness, 0.02)) * D.rgb;

	// Diffuse
	vec3	diffuse = vec3(NdotL0);
	float	LdotH = sqrt(max(LdotV * 0.5 + 0.5, 0.0));
	// Fresnel
	vec2	fDirect = textureLod(textureUnits[0], vec2(LdotH, NdotL0), 0.0).ba;
	spec *= mix(f0, vec3(1.0), fDirect.x);
	vec4	envLUT = textureLod(textureUnits[0], vec2(NdotV, roughness), 0.0);
	vec2	fDiff = vec2(fDirect.y, envLUT.b);
	fDiff = fDiff * (LdotH * LdotH * roughness * 2.0 - 0.5) + 1.0;
	diffuse *= (vec3(1.0) - f0) * fDiff.x * fDiff.y;

	// Environment
	vec3	refl = vec3(0.0);
	vec3	ambient = A.rgb;
	if ( hasCubeMap ) {
		float	m = roughness * (roughness * -4.0 + 10.0);
		refl = textureLod(CubeMap, reflectedWS, max(m, 0.0)).rgb;
		refl *= ambient;
		ambient *= textureLod(CubeMap2, normalWS, 0.0).rgb;
	} else {
		ambient /= 12.5;
		refl = ambient;
	}
	vec3	f = mix(f0, vec3(1.0), envLUT.r);
	if (!hasSpecular) {
		albedo = baseMap.rgb;
		diffuse = vec3(NdotL0);
		spec = vec3(0.0);
		f = vec3(0.0);
	} else {
		float	fDiffEnv = envLUT.b * ((NdotV + 1.0) * roughness - 0.5) + 1.0;
		ambient *= (vec3(1.0) - f0) * fDiffEnv;
	}
	float	ao = pbrMap.b;
	refl *= f * envLUT.g * ao;

	//vec3 soft = vec3(0.0);
	//float wrap = NdotL;
	//if ( hasSoftlight || subsurfaceRolloff > 0.0 ) {
	//	wrap = (wrap + subsurfaceRolloff) / (1.0 + subsurfaceRolloff);
	//	soft = albedo * max(0.0, wrap) * smoothstep(1.0, 0.0, sqrt(NdotL0));
	//
	//	diffuse += soft;
	//}

	// Diffuse
	color.rgb = diffuse * albedo * D.rgb;
	// Ambient
	color.rgb += ambient * albedo * ao;
	// Specular
	color.rgb += spec;
	color.rgb += refl;

	// Emissive
	color.rgb += emissive;

	color.rgb = tonemap(color.rgb * D.a, A.a);
	color.a = baseMap.a * alpha;

	fragColor = color;
}
