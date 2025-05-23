﻿//

#include <Chapter04/04_CubeMap/src/common.sp>

layout (location=0) out vec3 dir;

const vec3 pos[8] = vec3[8](
	vec3(-1.0,-1.0, 1.0),
	vec3( 1.0,-1.0, 1.0),
	vec3( 1.0, 1.0, 1.0),
	vec3(-1.0, 1.0, 1.0),

	vec3(-1.0,-1.0,-1.0),
	vec3( 1.0,-1.0,-1.0),
	vec3( 1.0, 1.0,-1.0),
	vec3(-1.0, 1.0,-1.0)
);

const int indices[36] = int[36](
	0, 1, 2, 2, 3, 0,	// front
	1, 5, 6, 6, 2, 1,	// right 
	7, 6, 5, 5, 4, 7,	// back
	4, 0, 3, 3, 7, 4,	// left
	4, 5, 1, 1, 0, 4,	// bottom
	3, 2, 6, 6, 7, 3	// top
);

void main() {
	int idx = indices[gl_VertexIndex];
	// mat4(mat3(pc.view) strips the translation part of the view matrix, only remains the rotation part
	// so that the skybox is always centered at and rendered around the camera, no matter where the camera moves
	// and the skybox will look boundless and infinite
	gl_Position = pc.proj * mat4(mat3(pc.view)) * vec4(1.0 * pos[idx], 1.0);
	dir = pos[idx].xyz;
}
