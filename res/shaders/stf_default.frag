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
	uint	blendMode;
	// 0 = "Red" (default), 1 = "Green", 2 = "Blue", 3 = "Alpha"
	uint	colorChannel;
	float	floatParams[5];
	bool	boolParams[8];
};

struct LayeredEmissivityComponent {
	bool	isEnabled;
	uint	firstLayerIndex;
	vec4	firstLayerTint;
	uint	firstLayerMaskIndex;
	bool	secondLayerActive;
	uint	secondLayerIndex;
	vec4	secondLayerTint;
	uint	secondLayerMaskIndex;
	uint	firstBlenderIndex;
	uint	firstBlenderMode;
	bool	thirdLayerActive;
	uint	thirdLayerIndex;
	vec4	thirdLayerTint;
	uint	thirdLayerMaskIndex;
	uint	secondBlenderIndex;
	uint	secondBlenderMode;
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
	uint	emissiveSourceLayer;
	vec4	emissiveTint;
	uint	emissiveMaskSourceBlender;
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
	uint	writeMask;
	bool	isPlanet;
	bool	isProjected;
	bool	useParallaxOcclusionMapping;
	sampler2D	surfaceHeightMap;
	float	parallaxOcclusionScale;
	bool	parallaxOcclusionShadows;
	uint	maxParralaxOcclusionSteps;
	uint	renderLayer;
	bool	useGBufferNormals;
	uint	blendMode;
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
	uint	blendingMode;
	bool	backLightingEnable;
	float	backlightingScale;
	float	backlightingSharpness;
	float	backlightingTransparencyFactor;
	vec4	backLightingTintColor;
	bool	depthMVFixup;
	bool	depthMVFixupEdgesOnly;
	bool	forceRenderBeforeOIT;
	uint	depthBiasInUlp;
};

struct OpacityComponent {
	uint	firstLayerIndex;
	bool	secondLayerActive;
	uint	secondLayerIndex;
	uint	firstBlenderIndex;
	// 0 = "Lerp", 1 = "Additive", 2 = "Subtractive", 3 = "Multiplicative"
	uint	firstBlenderMode;
	bool	thirdLayerActive;
	uint	thirdLayerIndex;
	uint	secondBlenderIndex;
	uint	secondBlenderMode;
	float	specularOpacityOverride;
};

struct AlphaSettingsComponent {
	bool	hasOpacity;
	float	alphaTestThreshold;
	uint	opacitySourceLayer;
	// 0 = "Linear" (default), 1 = "Additive", 2 = "PositionContrast", 3 = "None"
	uint	alphaBlenderMode;
	bool	useDetailBlendMask;
	bool	useVertexColor;
	uint	vertexColorChannel;
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
	uint	transmittanceSourceLayer;
};

struct TerrainSettingsComponent {
	bool	isEnabled;
	uint	textureMappingType;
	float	rotationAngle;
	float	blendSoftness;
	float	tilingDistance;
	float	maxDisplacement;
	float	displacementMidpoint;
};

struct LayeredMaterial {
	uint	shaderModel;
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

uniform sampler2D BaseMap;
uniform sampler2D NormalMap;
uniform sampler2D GlowMap;
uniform sampler2D ReflMap;
uniform sampler2D LightingMap;
uniform sampler2D GreyscaleMap;
uniform samplerCube CubeMap;

uniform vec4 solidColor;

uniform vec3 glowColor;
uniform float glowMult;

uniform float alpha;

uniform vec2 uvScale;
uniform vec2 uvOffset;

uniform bool hasEmit;
uniform bool hasGlowMap;
uniform bool hasCubeMap;

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
	vec4	a = vec4(-1.14821728, 2.88290570, -2.73468842, 1.0);
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
	vec4	a7 = vec4(-46.17083701, 68.74246739, -30.81940082, 4.71893658);
	vec4	a6 = vec4(223.01575965, -340.62859168, 148.74583666, -19.50419017);
	vec4	a5 = vec4(-438.85219655, 686.93794385, -296.61404938, 33.58358064);
	vec4	a4 = vec4(445.11796866, -715.92562804, 311.90860291, -31.69608504);
	vec4	a3 = vec4(-238.97873321, 398.18372723, -180.61901689, 18.38409556);
	vec4	a2 = vec4(59.14333584, -103.76723412, 51.62678633, -6.46862429);
	vec4	a1 = vec4(-2.33005272, 4.00815577, -1.95860285, 0.24186313);
	vec4	a0 = vec4(-1.04024587, 2.67572852, -2.62832432, 0.99366783);
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

vec4 colorLookup(float x, float y)
{
	return texture2D(GreyscaleMap, vec2(clamp(x, 0.0, 1.0), clamp(y, 0.0, 1.0)));
}

void main(void)
{
	if ( isWireframe ) {
		gl_FragColor = solidColor;
		return;
	}
	vec2 offset = gl_TexCoord[0].st * uvScale + uvOffset;

	vec4	baseMap = texture2D(BaseMap, offset);
	vec4	normalMap = texture2D(NormalMap, offset);
	vec4	lightingMap = vec4(0.25, 1.0, 0.0, 1.0);
	vec4	reflMap = texture2D(ReflMap, offset);
	vec4	glowMap = texture2D(GlowMap, offset);

	vec3 normal = normalMap.rgb;
	// Calculate missing blue channel
	normal.b = sqrt(1.0 - dot(normal.rg, normal.rg));
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

	vec4 color;
	vec3 albedo = baseMap.rgb * C.rgb;
	vec3 diffuse = A.rgb + D.rgb * NdotL0;

	// Emissive
	vec3 emissive = vec3(0.0);
	if ( hasEmit ) {
		emissive += glowColor * glowMult;
		if ( hasGlowMap ) {
			emissive *= glowMap.rgb;
		}
	} else if ( hasGlowMap ) {
		emissive += glowMap.rgb * glowMult;
	}
	emissive *= lightingMap.a;

	vec3	f0 = (reflMap.g == 0 && reflMap.b == 0) ? vec3(reflMap.r) : reflMap.rgb;
	f0 = max(f0, vec3(0.02));

	// Specular
	float	smoothness = lightingMap.r;
	float	roughness = max(1.0 - smoothness, 0.02);
	float	ao = roughness * roughness * 0.5;
	ao = lightingMap.g * NdotV / (NdotV + ao - (NdotV * ao));
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
