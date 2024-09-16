#version 120
#extension GL_ARB_shader_texture_lod : require

uniform sampler2D BaseMap;
uniform sampler2D GreyscaleMap;
uniform samplerCube CubeMap;
uniform sampler2D NormalMap;
uniform sampler2D EnvironmentMap;
uniform sampler2D ReflMap;
uniform sampler2D LightingMap;

uniform bool hasSourceTexture;
uniform bool hasGreyscaleMap;
uniform bool hasCubeMap;
uniform bool hasNormalMap;
uniform bool hasEnvMask;
uniform bool hasSpecularMap;

uniform bool greyscaleAlpha;
uniform bool greyscaleColor;

uniform bool useFalloff;
uniform bool hasRGBFalloff;

uniform bool hasWeaponBlood;

uniform vec4 glowColor;
uniform float glowMult;

uniform vec2 uvScale;
uniform vec2 uvOffset;

uniform vec4 falloffParams;
uniform float falloffDepth;

uniform float lightingInfluence;
uniform float envReflection;

uniform float fLumEmittance;

varying vec3 LightDir;
varying vec3 ViewDir;

varying vec4 A;
varying vec4 C;
varying vec4 D;

varying mat3 btnMatrix;
varying mat4 reflMatrix;

vec3 ViewDir_norm = normalize( ViewDir );
mat3 btnMatrix_norm = mat3( normalize( btnMatrix[0] ), normalize( btnMatrix[1] ), normalize( btnMatrix[2] ) );

vec4 colorLookup( float x, float y ) {

	return texture2D( GreyscaleMap, vec2( clamp(x, 0.0, 1.0), clamp(y, 0.0, 1.0)) );
}

vec3 fresnelSchlickRoughness(float NdotV, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
}

void main( void )
{
	vec2 offset = gl_TexCoord[0].st * uvScale + uvOffset;

	vec4 baseMap = texture2D( BaseMap, offset );
	vec4 normalMap = texture2D( NormalMap, offset );
	vec4 reflMap = texture2D(ReflMap, offset);

	vec3 normal = normalMap.rgb;
	// Calculate missing blue channel
	normal.b = sqrt(max(1.0 - dot(normal.rg, normal.rg), 0.0));
	normal = normalize( btnMatrix_norm * normal );
	if ( !gl_FrontFacing )
		normal *= -1.0;

	vec3 f0 = reflMap.rgb;
	vec3 L = normalize(LightDir);
	vec3 V = ViewDir_norm;
	vec3 R = reflect(-V, normal);
	vec3 H = normalize( L + V );

	float NdotL = max( dot(normal, L), 0.000001 );
	float NdotH = max( dot(normal, H), 0.000001 );
	float NdotV = max( abs(dot(normal, V)), 0.000001 );
	float LdotH = max( dot(L, H), 0.000001 );
	float NdotNegL = max( dot(normal, -L), 0.000001 );

	vec3 reflectedWS = vec3(reflMatrix * (gl_ModelViewMatrixInverse * vec4(R, 0.0))) * vec3(1.0, 1.0, -1.0);

	if ( greyscaleAlpha )
		baseMap.a = 1.0;

	vec4 baseColor = glowColor;
	if ( !greyscaleColor )
		baseColor.rgb *= glowMult;

	// Falloff
	float falloff = 1.0;
	if ( useFalloff || hasRGBFalloff ) {
		if ( falloffParams.y > (falloffParams.x + 0.000001) )
			falloff = smoothstep(falloffParams.x, falloffParams.y, NdotV);
		else if ( falloffParams.x > (falloffParams.y + 0.000001) )
			falloff = 1.0 - smoothstep(falloffParams.y, falloffParams.x, NdotV);
		else
			falloff = 0.5;
		falloff = clamp(mix(falloffParams.z, falloffParams.w, falloff), 0.0, 1.0);

		if ( useFalloff )
			baseMap.a *= falloff;

		if ( hasRGBFalloff )
			baseMap.rgb *= falloff;
	}

	float alphaMult = baseColor.a * baseColor.a;

	vec4 color;
	color.rgb = baseMap.rgb * C.rgb * baseColor.rgb;
	color.a = alphaMult * C.a * baseMap.a;

	if ( greyscaleColor ) {
		vec4 luG = colorLookup( texture2D( BaseMap, offset ).g, baseColor.r * C.r * falloff );

		color.rgb = luG.rgb;
	}

	if ( greyscaleAlpha ) {
		vec4 luA = colorLookup( texture2D( BaseMap, offset ).a, color.a );

		color.a = luA.a;
	}

	vec3 diffuse = A.rgb + (D.rgb * NdotL);
	color.rgb = mix( color.rgb, color.rgb * D.rgb, lightingInfluence );

	// Specular
	float g = 1.0;
	float s = 1.0;
	if ( hasSpecularMap ) {
		vec4 lightingMap = texture2D(LightingMap, offset);
		s = lightingMap.r;
		g = lightingMap.g;
	}
	float roughness = 1.0 - s;

	// Environment
	if ( hasCubeMap ) {
		float	m = roughness * (roughness * -4.0 + 10.0);
		vec3	cube = textureCubeLod( CubeMap, reflectedWS, max(m, 0.0) ).rgb;
		cube.rgb *= envReflection * g;
		cube.rgb = mix( cube.rgb, cube.rgb * D.rgb, lightingInfluence );
		if ( hasEnvMask )
			cube.rgb *= texture2D( EnvironmentMap, offset ).rgb;
		color.rgb += cube.rgb * falloff;
	}

	gl_FragColor.rgb = color.rgb * D.a;
	gl_FragColor.a = color.a;
}
