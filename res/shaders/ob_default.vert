#version 130

// 1 = use gl_FrontMaterial.ambient and gl_FrontMaterial.diffuse
// 2 = use gl_Color on A and D
// 4 = use gl_FrontMaterial.emission
// 8 = use gl_Color as emission
uniform int colorEmitMode;

out vec3 LightDir;
out vec3 ViewDir;

out vec4 ColorEA;
out vec4 ColorD;
out vec4 C;
out float toneMapScale;

out vec3 N;
out vec3 t;
out vec3 b;
out vec3 v;

void main( void )
{
	gl_Position = ftransform();
	gl_TexCoord[0] = gl_MultiTexCoord0;

	N = normalize(gl_NormalMatrix * gl_Normal);
	t = normalize(gl_NormalMatrix * gl_MultiTexCoord1.xyz);
	b = normalize(gl_NormalMatrix * gl_MultiTexCoord2.xyz);

	// NOTE: b<->t
	mat3 tbnMatrix = mat3(b.x, t.x, N.x,
						  b.y, t.y, N.y,
						  b.z, t.z, N.z);

	v = vec3(gl_ModelViewMatrix * gl_Vertex);

	ViewDir = tbnMatrix * -v.xyz;
	LightDir = tbnMatrix * gl_LightSource[0].position.xyz;

	toneMapScale = gl_LightSource[0].ambient.a;
	vec4 A = vec4( sqrt(gl_LightSource[0].ambient.rgb) * 0.375, 1.0 );
	vec4 D = vec4( sqrt(gl_LightSource[0].diffuse.rgb), 1.0 );

	if ( ( colorEmitMode & 1 ) != 0 ) {
		A *= gl_FrontMaterial.ambient;
		D *= gl_FrontMaterial.diffuse;
	}
	if ( ( colorEmitMode & 2 ) != 0 ) {
		A *= gl_Color;
		D *= gl_Color;
	}
	if ( ( colorEmitMode & 4 ) != 0 )
		A += gl_FrontMaterial.emission;
	if ( ( colorEmitMode & 8 ) != 0 )
		A += gl_Color;
	if ( ( colorEmitMode & 10 ) != 0 )
		C = vec4(1.0);
	else
		C = gl_Color;
	ColorEA = A;
	ColorD = D;
}
