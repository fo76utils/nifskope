#version 400 compatibility

out vec3 LightDir;
out vec3 ViewDir;
out vec3 NormalDir;

out vec4 A;
out vec4 D;

out mat4 reflMatrix;

mat4 rotateEnv( mat4 m, float rz )
{
	float	rz_c = cos(rz);
	float	rz_s = -sin(rz);
	return mat4(vec4(m[0][0] * rz_c - m[0][1] * rz_s,
					 m[0][0] * rz_s + m[0][1] * rz_c, m[0][2], m[0][3]),
				vec4(m[1][0] * rz_c - m[1][1] * rz_s,
					 m[1][0] * rz_s + m[1][1] * rz_c, m[1][2], m[1][3]),
				vec4(m[2][0] * rz_c - m[2][1] * rz_s,
					 m[2][0] * rz_s + m[2][1] * rz_c, m[2][2], m[2][3]),
				vec4(m[3][0] * rz_c - m[3][1] * rz_s,
					 m[3][0] * rz_s + m[3][1] * rz_c, m[3][2], m[3][3]));
}

void main( void )
{
	vec3 v = vec3(gl_ModelViewMatrix * gl_Vertex);
	NormalDir = normalize(gl_NormalMatrix * gl_Normal);

	reflMatrix = rotateEnv(mat4(1.0), gl_LightSource[0].position.w * 3.14159265);

	if (gl_ProjectionMatrix[3][3] == 1.0)
		v = vec3(0.0, 0.0, -1.0);	// orthographic view
	ViewDir = vec3(-v.xy, 1.0);
	LightDir = gl_LightSource[0].position.xyz;

	A = gl_LightSource[0].ambient;
	D = gl_LightSource[0].diffuse;

	gl_Position = vec4((gl_ModelViewProjectionMatrix * gl_Vertex).xy, 1.0, 1.0);
}
