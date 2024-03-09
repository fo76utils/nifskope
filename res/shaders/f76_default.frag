#version 130
#extension GL_ARB_shader_texture_lod : require

uniform sampler2D BaseMap;
uniform sampler2D NormalMap;
uniform sampler2D GlowMap;
uniform sampler2D ReflMap;
uniform sampler2D LightingMap;
uniform sampler2D GreyscaleMap;
uniform sampler2D EnvironmentMap;	// environment BRDF LUT
uniform samplerCube CubeMap;	// pre-filtered cube maps (specular, diffuse)
uniform samplerCube CubeMap2;

uniform vec4 solidColor;
uniform vec3 specColor;
uniform bool hasSpecular;
uniform float specStrength;
uniform float fresnelPower;

uniform float paletteScale;

uniform vec3 glowColor;
uniform float glowMult;

uniform float alpha;

uniform vec3 tintColor;

uniform vec2 uvScale;
uniform vec2 uvOffset;

uniform bool hasEmit;
uniform bool hasGlowMap;
uniform bool hasSoftlight;
uniform bool hasTintColor;
uniform bool hasCubeMap;
uniform bool hasSpecularMap;
uniform bool greyscaleColor;
uniform bool doubleSided;

uniform float subsurfaceRolloff;
uniform float rimPower;
uniform float backlightPower;

uniform float envReflection;

uniform bool isWireframe;
uniform bool isSkinned;
uniform mat4 worldMatrix;

in vec3 LightDir;
in vec3 ViewDir;

in vec4 A;
in vec4 C;
in vec4 D;

in mat3 btnMatrix;
in mat4 reflMatrix;

vec3 ViewDir_norm = normalize( ViewDir );
mat3 btnMatrix_norm = mat3( normalize( btnMatrix[0] ), normalize( btnMatrix[1] ), normalize( btnMatrix[2] ) );

#ifndef M_PI
	#define M_PI 3.1415926535897932384626433832795
#endif

#define FLT_EPSILON 1.192092896e-07F // smallest such that 1.0 + FLT_EPSILON != 1.0

vec3 fresnel_n(float LdotH, vec3 f0)
{
	vec4	a = vec4(-1.11050116, 2.79595384, -2.68545268, 1.0);
	float	f = ((a.r * LdotH + a.g) * LdotH + a.b) * LdotH + a.a;
	return mix(f0, vec3(1.0), f * f);
}

float fresnel_w(float LdotH)
{
	vec4	a = vec4(-1.30214688, 3.32294874, -2.87825095, 1.0);
	float	f = ((a.r * LdotH + a.g) * LdotH + a.b) * LdotH + a.a;
	return f * f;
}

vec3 LightingFuncGGX_REF(float NdotL, float LdotR, float NdotV, float LdotV, float roughness, vec3 F0)
{
	float alpha = roughness * roughness;
	// D (GGX normal distribution)
	float alphaSqr = alpha * alpha;
	// denom = NdotH * NdotH * (alphaSqr - 1.0) + 1.0,
	// LdotR = NdotH * NdotH * 2.0 - 1.0
	float denom = LdotR * alphaSqr + alphaSqr + (1.0 - LdotR);
	float D = alphaSqr / (denom * denom);
	// no pi because BRDF -> lighting
	// F (Fresnel term)
	vec3 F = fresnel_n(sqrt(max(LdotV * 0.5 + 0.5, 0.0)), F0);
	// G (remapped hotness, see Unreal Shading)
	float	k = (alpha + 2 * roughness + 1) / 8.0;
	float	G = NdotL / (mix(NdotL, 1, k) * mix(NdotV, 1, k));

	return D * F * G;
}

float OrenNayarFull(float NdotL, float NdotV, float LdotV, float roughness)
{
	float	NdotL0 = clamp(NdotL, FLT_EPSILON, 1.0);
	float	angleVN = acos(clamp(abs(NdotV), FLT_EPSILON, 1.0));
	float	angleLN = acos(NdotL0);

	float	alpha = max(angleVN, angleLN);
	float	beta = min(angleVN, angleLN);
	float	gamma = (LdotV - NdotL * NdotV) / sqrt(max((1.0 - NdotL * NdotL) * (1.0 - NdotV * NdotV), 0.000025));

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

float srgbCompress(float x)
{
	float	y = sqrt(max(x, 0.0) + 0.000858025);
	return (((y * -0.18732371 + 0.59302883) * y - 0.79095451) * y + 1.42598062) * y - 0.04110602;
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
	if ( hasSpecularMap )
		lightingMap = texture2D(LightingMap, offset);
	vec4	reflMap = texture2D(ReflMap, offset);
	vec4	glowMap = texture2D(GlowMap, offset);

	vec3 normal = normalMap.rgb;
	// Calculate missing blue channel
	normal.b = sqrt(max(1.0 - dot(normal.rg, normal.rg), 0.0));
	normal = normalize( btnMatrix_norm * normal );
	if ( !gl_FrontFacing && doubleSided )
		normal *= -1.0;

	vec3 L = normalize(LightDir);
	vec3 V = ViewDir_norm;
	vec3 R = reflect(-V, normal);

	float NdotL = dot(normal, L);
	float NdotL0 = max(NdotL, 0.0);
	float LdotR = dot(L, R);
	float NdotV = abs(dot(normal, V));
	float LdotV = dot(L, V);

	vec3	reflectedWS = vec3(reflMatrix * (gl_ModelViewMatrixInverse * vec4(R, 0.0))) * vec3(1.0, 1.0, -1.0);
	vec3	normalWS = vec3(reflMatrix * (gl_ModelViewMatrixInverse * vec4(normal, 0.0))) * vec3(1.0, 1.0, -1.0);

	vec4 color;
	vec3 albedo = baseMap.rgb * C.rgb;
	if ( greyscaleColor ) {
		// work around incorrect format used by Fallout 76 grayscale textures
		albedo = textureLod(GreyscaleMap, vec2(srgbCompress(baseMap.g), paletteScale * C.r), 0.0).rgb;
	}

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

	vec3	f0 = max(reflMap.rgb, vec3(0.02));

	// Specular
	float	roughness = 1.0 - lightingMap.r;
	vec3	spec = LightingFuncGGX_REF(NdotL0, LdotR, NdotV, LdotV, max(roughness, 0.02), f0) * D.rgb;

	// Diffuse
	float	diff = OrenNayarFull(NdotL, dot(normal, V), LdotV, roughness);
	vec3	diffuse = vec3(diff);

	// Environment
	vec3	refl = vec3(0.0);
	vec3	ambient = A.rgb;
	if ( hasCubeMap ) {
		float	m = roughness * (roughness * -4.0 + 10.0);
		refl = textureLod(CubeMap, reflectedWS, max(m, 0.0)).rgb;
		refl *= envReflection * specStrength;
		refl *= ambient;
		ambient *= textureLod(CubeMap2, normalWS, 0.0).rgb;
	} else {
		ambient /= 15.0;
		refl = ambient;
	}
	vec4	envLUT = textureLod(EnvironmentMap, vec2(NdotV, roughness), 0.0);
	vec3	f = mix(f0, vec3(1.0), envLUT.r);
	if (!hasSpecular) {
		albedo = max(albedo, reflMap.rgb);
		spec = vec3(0.0);
		f = vec3(0.0);
	}
	float	g = envLUT.g;
	float	ao = lightingMap.g;
	refl *= f * g * ao;
	albedo *= (vec3(1.0) - f);
	ao *= (envLUT.b * 0.5 + 0.625);

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

	color.rgb = tonemap(color.rgb * D.a, A.a);
	color.a = C.a * baseMap.a;

	gl_FragColor = color;
	gl_FragColor.a *= alpha;
}
