#version 120
#extension GL_ARB_shader_texture_lod : require

uniform samplerCube	CubeMap;
uniform bool	hasCubeMap;
uniform bool	invertZAxis;
uniform int	skyCubeMipLevel;

varying vec3 LightDir;
varying vec3 ViewDir;

varying vec4 A;
varying vec4 D;

varying mat4 reflMatrix;

float LightingFuncGGX_REF( float LdotR, float roughness )
{
	float alpha = roughness * roughness;
	// D (GGX normal distribution)
	float alphaSqr = alpha * alpha;
	// denom = NdotH * NdotH * (alphaSqr - 1.0) + 1.0,
	// LdotR = NdotH * NdotH * 2.0 - 1.0
	float denom = LdotR * alphaSqr + alphaSqr + (1.0 - LdotR);
	// no pi because BRDF -> lighting
	return alphaSqr / (denom * denom);
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

void main()
{
	vec3	L = normalize( LightDir );
	vec3	V = normalize( -ViewDir );

	float	VdotL = dot( V, L );
	float	VdotL0 = max( VdotL, 0.0 );

	vec3	viewWS = vec3( reflMatrix * (gl_ModelViewMatrixInverse * vec4(V, 0.0)) );
	if ( invertZAxis )
		viewWS.z *= -1.0;

	float	m = clamp( float(skyCubeMipLevel), 0.0, 6.0 );
	float	roughness = ( 5.0 - sqrt( 25.0 - 4.0 * m ) ) / 4.0;
	vec3	color = D.rgb * LightingFuncGGX_REF( VdotL, max(roughness, 0.02) ) * VdotL0;

	// Environment
	vec3	ambient = A.rgb;
	if ( hasCubeMap ) {
		color += textureCubeLod( CubeMap, viewWS, m ).rgb * ambient;
	} else {
		color += ambient * 0.08;
	}

	gl_FragColor = vec4( tonemap( color * D.a, A.a ), 0.0 );
}
