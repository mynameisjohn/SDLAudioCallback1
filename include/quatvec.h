#pragma once

#include "GL_Includes.h"
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

struct quatvec
{
	// This object is designed to represent some translation/rotation combo
	enum class Type
	{
		TR,	// T * R
		RT,	// R * T
		TRT	// T * R * Inv(T)
	};
	// Represent potential transforms
	fquat Q;
	vec3 V;
	Type T;

	// Constructors
	quatvec();
	quatvec( fquat q, vec3 v, Type t = Type::RT );
	quatvec( vec3 v, fquat q, Type t = Type::TR );

	// Setters
	void SetQ( fquat q );
	void SetV( vec3 v );
	void SetType( Type t );

	// Getters
	fquat GetQ() const;
	vec3 GetV() const;
	Type GetType() const;

	// Get transform matrix
	mat4 ToMat4() const;

	// Operators
	// TODO
};