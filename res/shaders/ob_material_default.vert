#version 130

out vec3 LightDir;
out vec3 ViewDir;
out vec3 HalfVector;

out vec4 ColorEA;
out vec4 ColorD;
out float toneMapScale;

out vec3 N;
out vec3 t;
out vec3 b;
out vec3 v;

out vec4 A;
out vec4 D;

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
	HalfVector = tbnMatrix * gl_LightSource[0].halfVector.xyz;

	toneMapScale = gl_LightSource[0].ambient.a;
	A = vec4( sqrt(gl_LightSource[0].ambient.rgb * gl_LightSource[0].diffuse.a) * 0.375, 1.0 );
	D = vec4( sqrt(gl_LightSource[0].diffuse.rgb * gl_LightSource[0].diffuse.a), 1.0 );

	ColorEA = gl_FrontMaterial.emission + gl_FrontMaterial.ambient * A;
	ColorD = gl_FrontMaterial.diffuse * D;
}
