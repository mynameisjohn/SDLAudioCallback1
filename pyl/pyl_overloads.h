#pragma once

#include "pyliason.h"
#include <glm/fwd.hpp>

namespace pyl
{
	bool convert( PyObject * o, glm::vec2& v );
	bool convert( PyObject * o, glm::vec3& v );
	bool convert( PyObject * o, glm::vec4& v );
	bool convert( PyObject * o, glm::fquat& v );
}