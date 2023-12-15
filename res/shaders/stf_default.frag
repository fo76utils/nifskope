#version 400 compatibility
#extension GL_ARB_shader_texture_lod : require

struct UVStream {
	vec2	scale;
	vec2	offset;
	bool	useChannelTwo;
};

struct TextureSet {
	// >= 1: textureUnits index + 1, -1: use replacement, 0: disabled
	int	textures[11];
	vec4	textureReplacements[11];
	float	floatParam;
};

struct Material {
	vec4	color;
	bool	colorModeLerp;
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
	bool	secondLayerActive;
	int	secondLayerIndex;
	vec4	secondLayerTint;
	int	secondLayerMaskIndex;
	int	firstBlenderIndex;
	int	firstBlenderMode;
	bool	thirdLayerActive;
	int	thirdLayerIndex;
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
	// >= 1: textureUnits index + 1, <= 0: disabled
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
uniform bool	hasCubeMap;
uniform float	envReflection;

uniform sampler2D	textureUnits[15];

uniform vec4 solidColor;

uniform bool isWireframe;
uniform bool isSkinned;
uniform mat4 worldMatrix;

uniform	LayeredMaterial	lm;

in vec3 LightDir;
in vec3 ViewDir;

in vec4 A;
in vec4 C;
in vec4 D;

in vec3 N;
in vec3 t;
in vec3 b;

in mat4 reflMatrix;

#ifndef M_PI
	#define M_PI 3.1415926535897932384626433832795
#endif

#define FLT_EPSILON 1.192092896e-07F // smallest such that 1.0 + FLT_EPSILON != 1.0

float G1V(float NdotV, float k)
{
	return 1.0 / (NdotV * (1.0 - k) + k);
}

float LightingFuncGGX_REF0(float NdotL, float NdotH, float NdotV, float LdotH, float roughness, float F0)
{
	float alpha = roughness * roughness;
	float F, D, vis;
	// D
	float alphaSqr = alpha * alpha;
	float denom = NdotH * NdotH * (alphaSqr - 1.0) + 1.0;
	D = alphaSqr / (M_PI * denom * denom);
	// F
	float LdotH5 = pow(1.0 - LdotH, 5);
	F = F0 + (1.0 - F0) * LdotH5;

	// V
	float k = alpha/2.0;
	vis = G1V(NdotL, k) * G1V(NdotV, k);

	float specular = NdotL * D * F * vis;
	return specular;
}

vec3 fresnel_n(float NdotV, vec3 f0)
{
	vec4	a = vec4(-1.11050116, 2.79595384, -2.68545268, 1.0);
	float	f = ((a.r * NdotV + a.g) * NdotV + a.b) * NdotV + a.a;
	return f0 + ((vec3(1.0) - f0) * f * f);
}

float fresnel_w(float NdotV)
{
	vec4	a = vec4(-1.30214688, 3.32294874, -2.87825095, 1.0);
	float	f = ((a.r * NdotV + a.g) * NdotV + a.b) * NdotV + a.a;
	return f * f;
}

vec3 fresnel_r(float NdotV, vec3 f0, float r)
{
	vec4	a7 = vec4(-36.86082892, 56.89686549, -22.98377259, 2.81411820);
	vec4	a6 = vec4(178.16930235, -277.12321903, 111.07929576, -11.70956117);
	vec4	a5 = vec4(-349.77072650, 551.07233810, -221.97439313, 20.29954802);
	vec4	a4 = vec4(351.42490382, -565.51251454, 233.58040120, -19.53778199);
	vec4	a3 = vec4(-183.56335374, 306.09790616, -134.37164019, 12.00737843);
	vec4	a2 = vec4(41.36210311, -73.56228236, 36.80732168, -4.61221961);
	vec4	a1 = vec4(0.25378228, -0.43450334, 0.18969042, -0.00880438);
	vec4	a0 = vec4(-1.11050116, 2.79595384, -2.68545268, 1.00000000);
	vec4	a = ((((((a7 * r + a6) * r + a5) * r + a4) * r + a3) * r + a2) * r + a1) * r + a0;
	float	f = ((a.r * NdotV + a.g) * NdotV + a.b) * NdotV + a.a;
	return f0 + ((vec3(1.0) - f0) * f * f);
}

vec3 LightingFuncGGX_REF(float NdotL, float NdotH, float NdotV, float LdotH, float roughness, vec3 F0)
{
	float alpha = roughness * roughness;
	// D (GGX normal distribution)
	float alphaSqr = alpha * alpha;
	float denom = NdotH * NdotH * (alphaSqr - 1.0) + 1.0;
	float D = alphaSqr / (denom * denom);
	// no pi because BRDF -> lighting
	// F (Fresnel term)
	vec3 F = fresnel_n(LdotH, F0);
	// G (remapped hotness, see Unreal Shading)
	float	k = (alpha + 2 * roughness + 1) / 8.0;
	float	G = NdotL / (mix(NdotL, 1, k) * mix(NdotV, 1, k));

	return D * F * G / 4.0;
}

float OrenNayar(vec3 L, vec3 V, vec3 N, float roughness, float NdotL)
{
	//float NdotL = dot(N, L);
	float NdotV = dot(N, V);
	float LdotV = dot(L, V);

	float rough2 = roughness * roughness;

	float A = 1.0 - 0.5 * (rough2 / (rough2 + 0.57));
	float B = 0.45 * (rough2 / (rough2 + 0.09));

	float a = min( NdotV, NdotL );
	float b = max( NdotV, NdotL );
	b = (sign(b) == 0.0) ? FLT_EPSILON : sign(b) * max( 0.01, abs(b) ); // For fudging the smoothness of C
	float C = sqrt( (1.0 - a * a) * (1.0 - b * b) ) / b;

	float gamma = LdotV - NdotL * NdotV;
	float L1 = A + B * max( gamma, FLT_EPSILON ) * C;

	return L1 * max( NdotL, FLT_EPSILON );
}

float OrenNayarFull(vec3 L, vec3 V, vec3 N, float roughness, float NdotL0)
{
	float	NdotV = max(dot(N, V), FLT_EPSILON);

	float	angleVN = acos(NdotV);
	float	angleLN = acos(NdotL0);

	float	alpha = max(angleVN, angleLN);
	float	beta = min(angleVN, angleLN);
	float	gamma = 0.0;
	//gamma = dot(L, V) - NdotL0 * NdotV;
	if ( beta > 0.005 )
		gamma = dot(normalize(cross(L, N)), normalize(cross(V, N)));

	float roughnessSquared = roughness * roughness;
	float roughnessSquared9 = (roughnessSquared / (roughnessSquared + 0.09));

	// C1, C2, and C3
	float C1 = 1.0 - 0.5 * (roughnessSquared / (roughnessSquared + 0.33));
	float C2 = 0.45 * roughnessSquared9;

	if( gamma >= 0.0 ) {
		C2 *= sin(alpha);
	} else {
		C2 *= (sin(alpha) - pow((2.0 * beta) / M_PI, 3.0));
	}

	float powValue = (4.0 * alpha * beta) / (M_PI * M_PI);
	float C3 = 0.125 * roughnessSquared9 * powValue * powValue;

	// Avoid asymptote at pi/2
	float asym = M_PI / 2.0;
	float lim1 = asym + 0.005;
	float lim2 = asym - 0.005;

	float ab2 = (alpha + beta) / 2.0;

	if ( beta >= asym && beta < lim1 )
		beta = lim1;
	else if ( beta < asym && beta >= lim2 )
		beta = lim2;

	if ( ab2 >= asym && ab2 < lim1 )
		ab2 = lim1;
	else if ( ab2 < asym && ab2 >= lim2 )
		ab2 = lim2;

	// Reflection
	float A = gamma * C2 * tan(beta);
	float B = (1.0 - abs(gamma)) * C3 * tan(ab2);

	float L1 = NdotL0 * (C1 + A + B);

	// Interreflection
	float twoBetaPi = 2.0 * beta / M_PI;
	float L2 = 0.17 * NdotL0 * (roughnessSquared / (roughnessSquared + 0.13)) * (1.0 - gamma * twoBetaPi * twoBetaPi);

	return L1 + L2;
}

float OrenNayarAmbient(float NdotV, float roughness)
{
	vec4	a4 = vec4(-0.61319173, 1.67989635, -1.74469001, 0.39303551);
	vec4	a3 = vec4(1.26009045, -3.45214521, 3.58530224, -0.41831188);
	vec4	a2 = vec4(-0.63577225, 1.74177294, -1.80896352, -0.30123290);
	vec4	a1 = vec4(-0.15710246, 0.43038707, -0.44698386, 0.26241189);
	vec4	a = (((a4 * roughness + a3) * roughness + a2) * roughness + a1) * roughness;
	return ((a.r * NdotV + a.g) * NdotV + a.b) * NdotV + a.a + 1.0;
}

vec3 fresnelSchlickRoughness(float NdotV, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
}

vec3 tonemap(vec3 x)
{
	float _A = 0.15;
	float _B = 0.50;
	float _C = 0.10;
	float _D = 0.20;
	float _E = 0.02;
	float _F = 0.30;

	return ((x*(_A*x+_C*_B)+_D*_E)/(x*(_A*x+_B)+_D*_F))-_E/_F;
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
	return texture2D(textureUnits[n - 1], offset);
}

float getBlenderMask(int n)
{
	float	r = 1.0;
	if ( lm.blenders[n].maskTexture != 0 ) {
		if ( lm.blenders[n].maskTexture < 0 ) {
			r = lm.blenders[n].maskTextureReplacement.r;
		} else {
			vec2	offset = getTexCoord(lm.blenders[n].uvStream);
			r = texture2D(textureUnits[lm.blenders[n].maskTexture - 1], offset).r;
		}
	}
	if ( lm.blenders[n].colorChannel >= 0 )
		r *= C[lm.blenders[n].colorChannel];
	return r;
}

void getLayer(int n, inout vec4 baseMap, inout vec3 normalMap, inout vec3 pbrMap)
{
	vec2	offset = getTexCoord(lm.layers[n].uvStream);
	// _height.dds
	if ( lm.layers[n].material.textureSet.textures[6] != 0 ) {
		// TODO: implement parallax mapping correctly
		vec3	V = normalize(ViewDir);
		float	heightMap = getLayerTexture(n, 6, offset).r;
		offset += heightMap * 0.025 * V.xy;
	}
	// _color.dds
	if ( lm.layers[n].material.textureSet.textures[0] != 0 )
		baseMap.rgb = getLayerTexture(n, 0, offset).rgb;
	baseMap.rgb *= lm.layers[n].material.color.rgb;
	// _normal.dds
	if ( lm.layers[n].material.textureSet.textures[1] != 0 ) {
		normalMap.rg = getLayerTexture(n, 1, offset).rg;
		// Calculate missing blue channel
		normalMap.b = sqrt(1.0 - dot(normalMap.rg, normalMap.rg));
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
		gl_FragColor = solidColor;
		return;
	}

	vec4	baseMap = vec4(0.0, 0.0, 0.0, 1.0);
	vec3	normal = vec3(0.0, 0.0, 1.0);
	vec3	pbrMap = vec3(0.75, 0.0, 1.0);	// roughness, metalness, AO
	float	alpha = 1.0;

	if ( lm.layersEnabled[0] ) {
		if ( lm.decalSettings.isDecal && lm.layers[0].material.textureSet.textures[0] == 0 )
			discard;
		getLayer( 0, baseMap, normal, pbrMap );
	}
	for (int i = 1; i < 4; i++) {
		if ( lm.layersEnabled[i] ) {
			vec4	layerBaseMap = baseMap;
			vec3	layerNormal = normal;
			vec3	layerPBRMap = pbrMap;
			getLayer(i, layerBaseMap, layerNormal, layerPBRMap);
			float	layerMask = getBlenderMask(i - 1);
			switch ( lm.blenders[i - 1].blendMode) {
				case 0:		// Linear
					baseMap.rgb = mix(baseMap.rgb, layerBaseMap.rgb, layerMask);
					baseMap.a *= layerBaseMap.a;
					normal = normalize(mix(normal, layerNormal, layerMask));
					pbrMap = mix(pbrMap, layerPBRMap, layerMask);
					break;
				case 1:		// Additive (TODO)
					break;
				case 2:		// PositionContrast (TODO)
					break;
			}
		}
	}

	if ( !gl_FrontFacing && lm.isTwoSided ) {
		normal *= -1.0;
	}
	// For _msn (Test with FSF1_Face)
	//normal.z = sqrt(1.0 - dot(normal.xy, normal.xy));

	vec3 L = normalize(LightDir);
	vec3 V = normalize(ViewDir);
	vec3 R = reflect(-L, normal);
	vec3 H = normalize(L + V);

	float NdotL = dot(normal, L);
	float NdotL0 = max(NdotL, FLT_EPSILON);
	float NdotH = max(dot(normal, H), FLT_EPSILON);
	float NdotV = max(abs(dot(normal, V)), FLT_EPSILON);
	float VdotH = max(dot(V, H), FLT_EPSILON);
	float LdotH = max(dot(L, H), FLT_EPSILON);
	float NdotNegL = max(dot(normal, -L), FLT_EPSILON);

	mat3	btn = transpose(mat3(b, t, N));
	vec3	reflectedWS = vec3(reflMatrix * (gl_ModelViewMatrixInverse * vec4(vec3(reflect(V, normal) * btn), 0.0)));
	reflectedWS.z = -reflectedWS.z;
	vec3	normalWS = vec3(reflMatrix * (gl_ModelViewMatrixInverse * vec4(vec3(-normal * btn), 0.0)));
	normalWS.z = -normalWS.z;

	if ( lm.alphaSettings.hasOpacity && lm.alphaSettings.opacitySourceLayer < 4 && lm.layersEnabled[lm.alphaSettings.opacitySourceLayer] ) {
		int	n = lm.alphaSettings.opacitySourceLayer;
		if ( lm.alphaSettings.useVertexColor )
			baseMap.a = C[lm.alphaSettings.vertexColorChannel];
		if ( lm.layers[n].material.textureSet.textures[2] != 0 )
			baseMap.a *= getLayerTexture(n, 2, getTexCoord(lm.alphaSettings.opacityUVstream)).r;
		alpha = lm.layers[n].material.color.a;
	}

	if ( lm.isEffect ) {
		if ( lm.hasOpacityComponent ) {
			int	n = lm.opacity.firstLayerIndex;
			if ( n < 4 && lm.layersEnabled[n] && lm.layers[n].material.textureSet.textures[2] != 0 )
				baseMap.a = getLayerTexture(n, 2, getTexCoord(lm.layers[n].uvStream)).r;
		}
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

	// TODO: layered emissivity
	vec3	emissive = vec3(0.0);
	if ( lm.emissiveSettings.isEnabled ) {
		int	n = lm.emissiveSettings.emissiveSourceLayer;
		if ( n <= 3 ) {
			if ( lm.layersEnabled[n] && lm.layers[n].material.textureSet.textures[7] != 0 ) {
				emissive = getLayerTexture(n, 7, getTexCoord(lm.layers[n].uvStream)).rgb;
				emissive *= lm.emissiveSettings.emissiveTint.rgb;
				emissive *= lm.emissiveSettings.emissiveTint.a * exp2(lm.emissiveSettings.exposureOffset * 0.5);
			}
		}
	}

	vec3	f0 = albedo * pbrMap.g * 0.96 + 0.04;
	albedo = albedo * (1.0 - pbrMap.g);

	// Specular
	float	smoothness = 1.0 - pbrMap.r;
	float	roughness = max(pbrMap.r, 0.02);
	vec3	spec = LightingFuncGGX_REF(NdotL0, NdotH, NdotV, LdotH, roughness, f0) * D.rgb;

	// Diffuse
	float	diff = OrenNayarFull(L, V, normal, 1.0 - smoothness, NdotL0);
	vec3	diffuse = vec3(diff);

	// Environment
	vec3	refl = vec3(0.0);
	vec3	ambient = A.rgb;
	if ( hasCubeMap ) {
		float	m = (1.0 - (smoothness * smoothness)) * 5.1041667;
		m = max(m, 6.0 - (smoothness * 7.0));
		refl = textureLod(CubeMap, reflectedWS, max(m, 0.0)).rgb;
		refl *= envReflection;
		refl *= ambient;
		ambient *= textureLod(CubeMap, normalWS, 5.0).rgb * envReflection;
	} else {
		ambient /= 15.0;
		refl = ambient;
	}
	vec3	f = fresnel_r(NdotV, f0, roughness);
	float	g = roughness * roughness * 0.5;
	g = NdotV / (NdotV + g - (NdotV * g));
	float	ao = pbrMap.b;
	refl *= f * g * ao;
	albedo *= (vec3(1.0) - f);
	ao *= OrenNayarAmbient(NdotV, 1.0 - smoothness);

	//vec3 soft = vec3(0.0);
	//float wrap = NdotL;
	//if ( hasSoftlight || subsurfaceRolloff > 0.0 ) {
	//	wrap = (wrap + subsurfaceRolloff) / (1.0 + subsurfaceRolloff);
	//	soft = albedo * max(0.0, wrap) * smoothstep(1.0, 0.0, sqrt(diff));
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

	color.rgb = tonemap(color.rgb) / tonemap(vec3(1.0));
	color.a = baseMap.a * alpha;

	gl_FragColor = color;
}
