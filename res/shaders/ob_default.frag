#version 130

uniform sampler2D BaseMap;
uniform sampler2D NormalMap;
uniform sampler2D GlowMap;

uniform bool isEffect;
uniform bool hasParallax;
uniform bool hasEmit;
uniform bool hasGlowMap;
uniform vec4 glowColor;
uniform float glowMult;

uniform vec2 uvCenter;
uniform vec2 uvScale;
uniform vec2 uvOffset;
uniform float uvRotation;

uniform vec4 falloffParams;

in vec3 LightDir;
in vec3 ViewDir;

in vec4 ColorEA;
in vec4 ColorD;
in vec4 C;
in float toneMapScale;

out vec4 fragColor;


vec3 tonemap(vec3 x)
{
	float a = 0.15;
	float b = 0.50;
	float c = 0.10;
	float d = 0.20;
	float e = 0.02;
	float f = 0.30;

	vec3 z = x * x * gl_LightSource[0].diffuse.a * (toneMapScale * 4.22978723);
	z = (z * (a * z + b * c) + d * e) / (z * (a * z + b) + d * f) - e / f;
	return sqrt(z / (toneMapScale * 0.93333333));
}

void main( void )
{
	vec3 L = normalize( LightDir );
	vec3 E = normalize( ViewDir );

	vec2 offset = gl_TexCoord[0].st - uvCenter;
	float r_c = cos( uvRotation );
	float r_s = sin( uvRotation ) * -1.0;
	offset = vec2( offset.x * r_c - offset.y * r_s, offset.x * r_s + offset.y * r_c ) * uvScale + uvCenter + uvOffset;
	if ( hasParallax )
		offset += E.xy * ( 0.015 - texture( BaseMap, offset ).a * 0.03 );

	vec4 baseMap = texture( BaseMap, offset );
	vec4 color = baseMap * C;

	if ( isEffect ) {
		float startO = min( falloffParams.z, 1.0 );
		float stopO = max( falloffParams.w, 0.0 );

		// TODO: When X and Y are both 0.0 or both 1.0 the effect is reversed.
		float falloff = smoothstep( falloffParams.y, falloffParams.x, abs(E.b) );
		falloff = mix( max(falloffParams.w, 0.0), min(falloffParams.z, 1.0), falloff );

		float alphaMult = glowColor.a * glowColor.a;

		color.rgb = color.rgb * glowColor.rgb * glowMult;
		color.a = color.a * falloff * alphaMult;
	} else {
		vec4 normalMap = texture( NormalMap, offset );

		vec3 normal = normalize( normalMap.rgb * 2.0 - 1.0 );
		if ( !gl_FrontFacing )
			normal *= -1.0;

		vec3 R = reflect( -L, normal );
		vec3 H = normalize( L + E );
		float NdotL = max( dot(normal, L), 0.0 );


		color.rgb *= ColorEA.rgb + ( ColorD.rgb * NdotL );
		color.a *= ColorD.a;


		// Emissive
		if ( hasGlowMap )
			color.rgb += baseMap.rgb * texture( GlowMap, offset ).rgb * glowColor.rgb * glowMult;
		else if ( hasEmit )
			color.rgb += baseMap.rgb * glowColor.rgb * glowMult;

		// Specular
		if ( NdotL > 0.0 && dot( gl_FrontMaterial.specular.rgb, gl_LightSource[0].specular.rgb ) > 0.0 ) {
			float NdotH = dot( normal, H );
			if ( NdotH > 0.0 ) {
				vec4 spec = gl_FrontMaterial.specular * pow( NdotH, gl_FrontMaterial.shininess );
				spec *= gl_LightSource[0].specular * normalMap.a;
				color.rgb += spec.rgb;
			}
		}
	}

	fragColor = vec4( tonemap( color.rgb ), color.a );
}
