/* Minimal TU that emits the sonicsv implementation. Used by the C++ smoke
 * test (tests/sonicsv_test.cpp), which links against this object so the
 * public header — and only the public header — has to be included from C++.
 * SONICSV_IMPLEMENTATION is passed via Makefile -D flag. */
#include "../sonicsv.h"
