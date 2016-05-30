#pragma once

#include "GL/glew.h"
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include "GL_Includes.h"

class Camera
{
public:
	Camera();
	void InitOrtho( vec2 X, vec2 Y );

	void Reset();
	void Translate( vec2 T );

	// Functions to access camera info
	mat4 GetMat();
	mat4 GetTransform();
	mat4 GetProj();

	// Access to static shader handles
	static GLint GetProjHandle();
	static GLint GetPosHandle();

	static void SetProjHandle( GLint p );
	static void SetPosHandle( GLint C );

private:
	// Camera Type, position, rotation, projection
	vec2 m_v2Pos;
	mat4 m_m4Proj;

	// Static shader handles
	static GLint s_ProjHandle;
	static GLint s_PosHandle;

public:
	static const std::string strModuleName;
	static void pylExpose();
};