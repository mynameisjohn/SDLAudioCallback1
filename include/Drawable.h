#pragma once

#include "GL_Includes.h"
#include "quatvec.h"

#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>

#include <map>

// TODO
// Make a static container in Drawable
// that holds generic primitives (like
// circle, quad, etc.) that can be 
// drawn on request via static functions

class Drawable {
	std::string m_SrcFile;
	GLuint m_VAO;
	GLuint m_nIdx;
	vec4 m_Color;
	quatvec m_QV;
	vec2 m_Scale; // Could be a part of quatvec...
public:
	Drawable();
	Drawable(std::string iqmSrc, vec4 clr, quatvec qv, vec2 scale);

	mat4 GetMV() const;
	vec4 GetColor() const;
	
	void Draw();

	void SetPos(vec3 T);
	void Translate(vec3 T);

	void SetRot(fquat Q);
	void Rotate(fquat Q);

	void SetTransform(quatvec QV);
	void Transform(quatvec QV);

	void SetColor(vec4 C);
	
	static void SetPosHandle( GLint );
	static void SetColorHandle( GLint );

private:
	// Static VAO cache (string to VAO/nIdx)
	static std::map<std::string, std::array<GLuint, 2> > s_VAOCache;
	// Static Drawable Cache for common primitives
	static std::map<std::string, Drawable> s_PrimitiveMap;

	static GLint s_PosHandle;
	static GLint s_ColorHandle;

public:
	static bool DrawPrimitive(std::string prim);

	static const std::string strModuleName;
	static void pylExpose();
};