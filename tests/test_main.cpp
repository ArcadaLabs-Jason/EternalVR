// Shared doctest entry point, linked into every test executable.

// doctest 2.4.12's implementation uses DBL_EPSILON but relies on another standard header to have
// pulled in <cfloat>, which recent libc++ versions no longer do.
#include <cfloat>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
