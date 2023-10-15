#version 130
#extension GL_ARB_shader_texture_lod : require

struct UVStream {
	vec2	scale;
	vec2	offset;
	bool	useChannelTwo;
};

struct TextureSet {
	sampler2D	textures[21];
	bool	texturesEnabled[21];
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
	sampler2D	maskTexture;
	bool	maskTextureEnabled;
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
	bool	surfaceHeightMapEnabled;
	sampler2D	surfaceHeightMap;
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

struct OpacityComponent {
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

struct AlphaSettingsComponent {
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
	bool	isTwoSided;
	bool	layersEnabled[6];
	Layer	layers[6];
	Blender	blenders[5];
	LayeredEmissivityComponent	layeredEmissivity;
	EmissiveSettingsComponent	emissiveSettings;
	DecalSettingsComponent	decalSettings;
	EffectSettingsComponent	effectSettings;
	OpacityComponent	opacity;
	AlphaSettingsComponent	alphaSettings;
	TranslucencySettingsComponent	translucencySettings;
	TerrainSettingsComponent	terrainSettings;
};

uniform samplerCube CubeMap;
uniform bool hasCubeMap;

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
	float k = alpha/2.0f;
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
	float k = (alpha + 2 * roughness + 1) / 8.0;
	float G = NdotL * NdotV / (mix(NdotL, 1, k) * mix(NdotV, 1, k));

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

float OrenNayarFull(vec3 L, vec3 V, vec3 N, float roughness, float NdotL)
{
	//float NdotL = dot(N, L);
	float NdotV = dot(N, V);
	float LdotV = dot(L, V);

	float angleVN = acos(max(NdotV, FLT_EPSILON));
	float angleLN = acos(max(NdotL, FLT_EPSILON));

	float alpha = max(angleVN, angleLN);
	float beta = min(angleVN, angleLN);
	float gamma = LdotV - NdotL * NdotV;

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
	float lim1 = asym + 0.01;
	float lim2 = asym - 0.01;

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

	float L1 = max(FLT_EPSILON, NdotL) * (C1 + A + B);

	// Interreflection
	float twoBetaPi = 2.0 * beta / M_PI;
	float L2 = 0.17 * max(FLT_EPSILON, NdotL) * (roughnessSquared / (roughnessSquared + 0.13)) * (1.0 - gamma * twoBetaPi * twoBetaPi);

	return L1 + L2;
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

void getLayer(int n, inout vec4 baseMap, inout vec3 normalMap, inout vec3 pbrMap, inout vec4 emissiveMap)
{
	vec2	offset;
	if ( lm.layers[n].uvStream.useChannelTwo )
		offset = gl_TexCoord[1].st;	// this may be incorrect
	else
		offset = gl_TexCoord[0].st;
	offset = offset * lm.layers[n].uvStream.scale + lm.layers[n].uvStream.offset;
	// _height.dds
	// TODO: implement parallax mapping
//if ( lm.layers[n].material.textureSet.texturesEnabled[6] )
//	float	heightMap = texture2D(lm.layers[n].material.textureSet.textures[6], offset).r;
	// _color.dds
	if ( lm.layers[n].material.textureSet.texturesEnabled[0] )
		baseMap.rgb = texture2D(lm.layers[n].material.textureSet.textures[0], offset).rgb;
	// _normal.dds
	if ( lm.layers[n].material.textureSet.texturesEnabled[1] ) {
		normalMap.rg = texture2D(lm.layers[n].material.textureSet.textures[1], offset).rg;
		// Calculate missing blue channel
		normalMap.b = sqrt(1.0 - dot(normalMap.rg, normalMap.rg));
	}
	// _opacity.dds
	if ( lm.layers[n].material.textureSet.texturesEnabled[2] )
		baseMap.a = texture2D(lm.layers[n].material.textureSet.textures[2], offset).r;
	// _rough.dds
	if ( lm.layers[n].material.textureSet.texturesEnabled[3] )
		pbrMap.r = texture2D(lm.layers[n].material.textureSet.textures[3], offset).r;
	// _metal.dds
	if ( lm.layers[n].material.textureSet.texturesEnabled[4] )
		pbrMap.g = texture2D(lm.layers[n].material.textureSet.textures[4], offset).r;
	// _ao.dds
	if ( lm.layers[n].material.textureSet.texturesEnabled[5] )
		pbrMap.b = texture2D(lm.layers[n].material.textureSet.textures[5], offset).r;
	// _emissive.dds
	if ( lm.layers[n].material.textureSet.texturesEnabled[7] )
		emissiveMap.rgb = texture2D(lm.layers[n].material.textureSet.textures[7], offset).rgb;
	// _transmissive.dds
	if ( lm.layers[n].material.textureSet.texturesEnabled[8] )
		emissiveMap.a = texture2D(lm.layers[n].material.textureSet.textures[8], offset).r;
}

void main(void)
{
	if ( isWireframe ) {
		gl_FragColor = solidColor;
		return;
	}

	vec4	baseMap = vec4(0.0, 0.0, 0.0, 1.0);
	vec3	normalMap = vec3(0.0, 0.0, 1.0);
	vec3	pbrMap = vec3(0.75, 0.0, 0.9);	// roughness, metalness, AO
	vec4	emissiveMap = vec4(0.0, 0.0, 0.0, 0.0);	// alpha = translucency
	float	alpha = 1.0;

	for (int i = 0; i < 6; i++) {
		if ( lm.layersEnabled[i] ) {
			getLayer(i, baseMap, normalMap, pbrMap, emissiveMap);
			break;
		}
	}

	vec3 normal = normalMap.rgb;
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

	vec3 reflected = reflect(V, normal);
	vec3 reflectedVS = b * reflected.x + t * reflected.y + N * reflected.z;
	vec3 reflectedWS = vec3(reflMatrix * (gl_ModelViewMatrixInverse * vec4(reflectedVS, 0.0)));
	reflectedWS.z = -reflectedWS.z;

	vec4	color;
	vec3	albedo = baseMap.rgb;
	vec3	diffuse = A.rgb + D.rgb * NdotL0;

	// TODO: Emissive
	vec3	emissive = vec3(0.0);
	//if ( hasEmit ) {
	//	emissive += glowColor * glowMult;
	//	if ( hasGlowMap ) {
	//		emissive *= glowMap.rgb;
	//	}
	//} else if ( hasGlowMap ) {
	//	emissive += glowMap.rgb * glowMult;
	//}
	//emissive *= lightingMap.a;

	vec3	f0 = albedo * pbrMap.g * 0.96 + 0.04;
	albedo = albedo * (1.0 - pbrMap.g);

	// Specular
	float	smoothness = 1.0 - pbrMap.r;
	float	roughness = max(pbrMap.r, 0.02);
	float	ao = roughness * roughness * 0.5;
	ao = pbrMap.b * NdotV / (NdotV + ao - (NdotV * ao));
	vec3	spec = LightingFuncGGX_REF(NdotL0, NdotH, NdotV, LdotH, roughness, f0) * NdotL0 * D.rgb;

	// Diffuse
	float diff = OrenNayarFull(L, V, normal, 1.0 - smoothness, NdotL);
	diffuse = vec3(diff);

	// Environment
	vec3	refl = vec3(0.0);
	vec3	ambient = A.rgb / 0.75;
	if ( hasCubeMap ) {
		float	cubeScale = 1.0;	// 0.00052201 for cell_cityplazacube.dds
		refl = textureLod(CubeMap, reflectedWS, 8.0 - smoothness * 8.0).rgb;
		refl *= ao * cubeScale;
		refl *= ambient;
		ambient *= textureLod(CubeMap, reflectedWS, 8.0).rgb * cubeScale;
	} else {
		refl = vec3(0.05) * ambient * ao;
		ambient *= 0.05;
	}
	vec3	f = fresnel_r(NdotV, f0, roughness);
	refl *= f;
	albedo *= (vec3(1.0) - f);

	//vec3 soft = vec3(0.0);
	//float wrap = NdotL;
	//if ( hasSoftlight || subsurfaceRolloff > 0.0 ) {
	//	wrap = (wrap + subsurfaceRolloff) / (1.0 + subsurfaceRolloff);
	//	soft = albedo * max(0.0, wrap) * smoothstep(1.0, 0.0, sqrt(diff));
	//
	//	diffuse += soft;
	//}

	//if ( hasTintColor ) {
	//	albedo *= tintColor;
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
	color.a = C.a * baseMap.a;

	gl_FragColor = color;
	gl_FragColor.a *= alpha;
}
