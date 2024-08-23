#version 120

varying vec3 LightDir;
varying vec3 ViewDir;

varying mat3 btnMatrix;

varying vec4 A;
varying vec4 C;
varying vec4 D;

varying mat4 reflMatrix;

uniform bool isSkinned;
uniform bool isGPUSkinned;
uniform mat4 boneTransforms[100];
uniform mat4 worldMatrix;

mat4 inverse(mat4 m) {
  float
      a00 = m[0][0], a01 = m[0][1], a02 = m[0][2], a03 = m[0][3],
      a10 = m[1][0], a11 = m[1][1], a12 = m[1][2], a13 = m[1][3],
      a20 = m[2][0], a21 = m[2][1], a22 = m[2][2], a23 = m[2][3],
      a30 = m[3][0], a31 = m[3][1], a32 = m[3][2], a33 = m[3][3],

      b00 = a00 * a11 - a01 * a10,
      b01 = a00 * a12 - a02 * a10,
      b02 = a00 * a13 - a03 * a10,
      b03 = a01 * a12 - a02 * a11,
      b04 = a01 * a13 - a03 * a11,
      b05 = a02 * a13 - a03 * a12,
      b06 = a20 * a31 - a21 * a30,
      b07 = a20 * a32 - a22 * a30,
      b08 = a20 * a33 - a23 * a30,
      b09 = a21 * a32 - a22 * a31,
      b10 = a21 * a33 - a23 * a31,
      b11 = a22 * a33 - a23 * a32,

      det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;

  return mat4(
      a11 * b11 - a12 * b10 + a13 * b09,
      a02 * b10 - a01 * b11 - a03 * b09,
      a31 * b05 - a32 * b04 + a33 * b03,
      a22 * b04 - a21 * b05 - a23 * b03,
      a12 * b08 - a10 * b11 - a13 * b07,
      a00 * b11 - a02 * b08 + a03 * b07,
      a32 * b02 - a30 * b05 - a33 * b01,
      a20 * b05 - a22 * b02 + a23 * b01,
      a10 * b10 - a11 * b08 + a13 * b06,
      a01 * b08 - a00 * b10 - a03 * b06,
      a30 * b04 - a31 * b02 + a33 * b00,
      a21 * b02 - a20 * b04 - a23 * b00,
      a11 * b07 - a10 * b09 - a12 * b06,
      a00 * b09 - a01 * b07 + a02 * b06,
      a31 * b01 - a30 * b03 - a32 * b00,
      a20 * b03 - a21 * b01 + a22 * b00) / det;
}

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
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
	gl_TexCoord[0] = gl_MultiTexCoord0;

	vec3 v;
	if ( !isGPUSkinned ) {
		btnMatrix[2] = normalize(gl_NormalMatrix * gl_Normal);
		btnMatrix[1] = normalize(gl_NormalMatrix * gl_MultiTexCoord1.xyz);
		btnMatrix[0] = normalize(gl_NormalMatrix * gl_MultiTexCoord2.xyz);
		v = vec3(gl_ModelViewMatrix * gl_Vertex);
		reflMatrix = worldMatrix;
		if ( isSkinned ) {
			mat4 bt = boneTransforms[int(gl_MultiTexCoord3[0])] * gl_MultiTexCoord4[0];
			bt += boneTransforms[int(gl_MultiTexCoord3[1])] * gl_MultiTexCoord4[1];
			bt += boneTransforms[int(gl_MultiTexCoord3[2])] * gl_MultiTexCoord4[2];
			bt += boneTransforms[int(gl_MultiTexCoord3[3])] * gl_MultiTexCoord4[3];
			reflMatrix = inverse(bt);
		}
	} else if ( isSkinned && isGPUSkinned ) {
		mat4 bt = boneTransforms[int(gl_MultiTexCoord3[0])] * gl_MultiTexCoord4[0];
		bt += boneTransforms[int(gl_MultiTexCoord3[1])] * gl_MultiTexCoord4[1];
		bt += boneTransforms[int(gl_MultiTexCoord3[2])] * gl_MultiTexCoord4[2];
		bt += boneTransforms[int(gl_MultiTexCoord3[3])] * gl_MultiTexCoord4[3];

		vec4 V = bt * gl_Vertex;
		vec3 normal = vec3(bt * vec4(gl_Normal, 0.0));
		vec3 tan = vec3(bt * vec4(gl_MultiTexCoord1.xyz, 0.0));
		vec3 bit = vec3(bt * vec4(gl_MultiTexCoord2.xyz, 0.0));

		gl_Position = gl_ModelViewProjectionMatrix * V;
		btnMatrix[2] = normalize(gl_NormalMatrix * normal);
		btnMatrix[1] = normalize(gl_NormalMatrix * tan);
		btnMatrix[0] = normalize(gl_NormalMatrix * bit);
		v = vec3(gl_ModelViewMatrix * V);

		reflMatrix = inverse(bt);
	} else {
		btnMatrix[2] = normalize(gl_NormalMatrix * gl_Normal);
		btnMatrix[1] = normalize(gl_NormalMatrix * gl_MultiTexCoord1.xyz);
		btnMatrix[0] = normalize(gl_NormalMatrix * gl_MultiTexCoord2.xyz);
		v = vec3(gl_ModelViewMatrix * gl_Vertex);
		reflMatrix = worldMatrix;
	}

	reflMatrix = rotateEnv(reflMatrix, gl_LightSource[0].position.w * 3.14159265);

	if (gl_ProjectionMatrix[3][3] == 1.0)
		v = vec3(0.0, 0.0, -1.0);	// orthographic view
	ViewDir = -v.xyz;
	LightDir = gl_LightSource[0].position.xyz;

	A = gl_LightSource[0].ambient;
	C = gl_Color;
	D = gl_LightSource[0].diffuse;
}
