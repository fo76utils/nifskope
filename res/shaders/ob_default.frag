#version 120

uniform sampler2D BaseMap;
uniform sampler2D NormalMap;
uniform sampler2D GlowMap;
uniform sampler2D HeightMap;
uniform samplerCube CubeMap;
uniform sampler2D EnvironmentMap;

// bits 0 to 2: color mode
// bit 3: lighting mode
// bits 4 to 5: vertex mode
uniform int vertexColorFlags;

uniform bool isEffect;
uniform bool hasSpecular;
uniform bool hasEmit;
uniform bool hasGlowMap;
uniform bool hasCubeMap;
uniform bool hasCubeMask;
uniform float cubeMapScale;
// <= 0: disabled, 1: simple parallax mapping using BaseMap.a, >= 2: POM using HeightMap.r
uniform int parallaxMaxSteps;
uniform float parallaxScale;
uniform float glowMult;
uniform vec4 glowColor;

uniform vec2 uvCenter;
uniform vec2 uvScale;
uniform vec2 uvOffset;
uniform float uvRotation;

uniform vec4 falloffParams;

uniform mat4 worldMatrix;

varying vec3 LightDir;
varying vec3 ViewDir;

varying vec4 C;
varying vec4 D;
varying vec4 A;
varying float toneMapScale;


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

// parallax occlusion mapping based on code from
// https://web.archive.org/web/20150419215321/http://sunandblackcat.com/tipFullView.php?l=eng&topicid=28

vec2 parallaxMapping( vec3 V, vec2 offset )
{
	if ( parallaxMaxSteps < 2 )
		return offset + V.xy * ( ( 0.5 - texture2D( BaseMap, offset ).a ) * parallaxScale );

	// determine optimal height of each layer
	float	layerHeight = 1.0 / mix( max( float(parallaxMaxSteps), 4.0 ), 4.0, abs(V.z) );

	// current height of the layer
	float	curLayerHeight = 1.0;
	vec2	dtex = parallaxScale * V.xy / max( abs(V.z), 0.02 );
	// current texture coordinates
	vec2	currentTextureCoords = offset + ( dtex * 0.5 );
	// shift of texture coordinates for each layer
	dtex *= layerHeight;

	// height from heightmap
	float	heightFromTexture = texture2D( HeightMap, currentTextureCoords ).r;

	// while point is above the surface
	while ( curLayerHeight > heightFromTexture ) {
		// to the next layer
		curLayerHeight -= layerHeight;
		// shift of texture coordinates
		currentTextureCoords -= dtex;
		// new height from heightmap
		heightFromTexture = texture2D( HeightMap, currentTextureCoords ).r;
	}

	// previous texture coordinates
	vec2	prevTCoords = currentTextureCoords + dtex;

	// heights for linear interpolation
	float	nextH = curLayerHeight - heightFromTexture;
	float	prevH = curLayerHeight + layerHeight - texture2D( HeightMap, prevTCoords ).r;

	// proportions for linear interpolation
	float	weight = nextH / ( nextH - prevH );

	// return interpolation of texture coordinates
	return mix( currentTextureCoords, prevTCoords, weight );
}

void main( void )
{
	vec3 L = normalize( LightDir );
	vec3 E = normalize( ViewDir );

	vec2 offset = gl_TexCoord[0].st - uvCenter;
	float r_c = cos( uvRotation );
	float r_s = sin( uvRotation ) * -1.0;
	offset = vec2( offset.x * r_c - offset.y * r_s, offset.x * r_s + offset.y * r_c ) * uvScale + uvCenter + uvOffset;
	if ( parallaxMaxSteps >= 1 && parallaxScale >= 0.0005 )
		offset = parallaxMapping( E, offset );

	vec4 baseMap = texture2D( BaseMap, offset );
	vec4 color = baseMap;

	if ( isEffect ) {
		float alphaMult = glowColor.a * glowColor.a;

		if ( falloffParams.y != falloffParams.x ) {
			// TODO: When X and Y are both 0.0 or both 1.0 the effect is reversed.
			float falloff = smoothstep( falloffParams.y, falloffParams.x, abs(E.z) );
			falloff = mix( max(falloffParams.w, 0.0), min(falloffParams.z, 1.0), falloff );
			alphaMult *= falloff;
		}

		color *= C;
		color.rgb = color.rgb * glowColor.rgb * glowMult;
		color.a = color.a * alphaMult;
	} else {
		vec4 normalMap = texture2D( NormalMap, offset );

		vec3 normal = normalize( normalMap.rgb * 2.0 - 1.0 );
		if ( !gl_FrontFacing )
			normal *= -1.0;

		vec3 R = reflect( -L, normal );
		vec3 H = normalize( L + E );
		float NdotL = max( dot(normal, L), 0.0 );

		// work around the lack of bitwise operators in GLSL 1.20
		int tmp = vertexColorFlags;
		bool vcfBit5 = ( tmp >= 32 );
		if ( vcfBit5 )
			tmp -= 32;
		bool vcfBit4 = ( tmp >= 16 );
		if ( vcfBit4 )
			tmp -= 16;
		bool vcfBit3 = ( tmp >= 8 );

		if ( vcfBit3 && vcfBit5 ) {
			color *= C;
			color.rgb *= A.rgb + ( D.rgb * NdotL );
		} else if ( vcfBit3 ) {
			color.rgb *= ( A.rgb * gl_FrontMaterial.ambient.rgb ) + ( D.rgb * gl_FrontMaterial.diffuse.rgb * NdotL );
			color.a *= min( gl_FrontMaterial.ambient.a + gl_FrontMaterial.diffuse.a, 1.0 );
		} else {
			color.rgb *= A.rgb + ( D.rgb * NdotL );
		}


		// Environment
		if ( hasCubeMap && cubeMapScale > 0.0 ) {
			vec3 R = reflect( -E, normal );
			vec3 reflectedWS = vec3( worldMatrix * ( gl_ModelViewMatrixInverse * vec4( R, 0.0 ) ) );
			vec3 cube = textureCube( CubeMap, reflectedWS ).rgb;
			if ( hasCubeMask )
				cube *= texture2D( EnvironmentMap, offset ).r * cubeMapScale;
			else
				cube *= normalMap.a * cubeMapScale;
			color.rgb += cube * sqrt( gl_LightSource[0].ambient.rgb );
		}

		// Emissive
		vec3 emissive = glowColor.rgb * glowMult;
		if ( !vcfBit4 )
			emissive *= gl_FrontMaterial.emission.rgb;
		else
			emissive *= C.rgb;
		if ( hasGlowMap )
			color.rgb += emissive * baseMap.rgb * texture2D( GlowMap, offset ).rgb;
		else if ( hasEmit )
			color.rgb += emissive * baseMap.rgb;

		// Specular
		if ( hasSpecular && NdotL > 0.0 ) {
			float NdotH = dot( normal, H );
			if ( NdotH > 0.0 ) {
				vec4 spec = gl_FrontMaterial.specular * pow( NdotH, gl_FrontMaterial.shininess );
				color.rgb += spec.rgb * normalMap.a;
			}
		}
	}

	gl_FragColor = vec4( tonemap( color.rgb ), color.a );
}
