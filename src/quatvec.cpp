#include "quatvec.h"

#include <glm/gtx/transform.hpp>

#include <chrono>

quatvec::quatvec() :
	Q(cos(0), 0, 0, sin(0)),
	V(0),
	T(Type::TR)
{}

quatvec::quatvec(fquat q, vec3 v, Type t) :
	Q(q),
	V(v),
	T(t)
{}

quatvec::quatvec(vec3 v, fquat q, Type t) :
	Q(q),
	V(v),
	T(t)
{}

void quatvec::SetQ(fquat q) {
	Q = q;
}

void quatvec::SetV(vec3 v) {
	V = v;
}

void quatvec::SetType(Type t) {
	T = t;
}

fquat quatvec::GetQ() const {
	return Q;
}

vec3 quatvec::GetV() const {
	return V;
}

quatvec::Type quatvec::GetType() const {
	return T;
}

mat4 quatvec::ToMat4() const {
	switch (T) {
	case Type::TR:
		return glm::translate(V) * glm::mat4_cast(Q);
	case Type::RT:
		return glm::mat4_cast(Q) * glm::translate(V);
	case Type::TRT:
		return glm::translate(V) * glm::mat4_cast(Q) * glm::translate(-V);
	}
	return mat4(1);
}