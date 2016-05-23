#pragma once

#include "pyliason.h"

// This shows an example of how to create a custom
// conversion to a C++ type from a PyType
// Problems: If you've registered a type and
// would like to convert a pointer of that type, you're
// shit out of luck. This should be reserved for POD
// types that you'd like to pass back and forth
// between the interpreter and host code

