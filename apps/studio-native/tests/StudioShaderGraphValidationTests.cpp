#include "apps/studio-native/StudioShaderGraphValidation.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace aiverse::studio;
int main() {
 assert(validStudioShaderGraphName("mat_tint-1"));
 assert(validStudioShaderGraphName("A"));
 assert(!validStudioShaderGraphName("../secret"));
 assert(!validStudioShaderGraphName("foo/bar"));
 assert(!validStudioShaderGraphName("foo.bar"));
 assert(!validStudioShaderGraphName(""));
 assert(!validStudioShaderGraphName(std::string(65,'z')));
 assert(!validStudioShaderGraphName("name space"));
 std::cout<<"Studio Shader Graph name validation PASS\n";
}
