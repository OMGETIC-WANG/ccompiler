#define ENABLED 1
#define FUNCTION(value) (value)

#ifdef ENABLED
int ifdef_defined = 1;
#else
int ifdef_defined_else = 0;
#endif

#ifdef FUNCTION
int ifdef_function_like = 2;
#endif

#ifdef MISSING
int ifdef_missing_then = 0;
#else
int ifdef_missing_else = 3;
#endif

#ifndef MISSING
int ifndef_missing_then = 4;
#else
int ifndef_missing_else = 0;
#endif

#ifndef ENABLED
int ifndef_defined_then = 0;
#else
int ifndef_defined_else = 5;
#endif

#ifdef MISSING
#define IFDEF_LEAKED 1
#include "MissingIfdefHeader.h"
#endif

#ifdef IFDEF_LEAKED
int skipped_define_leak = 0;
#endif

#ifdef MISSING
#ifdef ENABLED
int nested_inactive_leak = 0;
#endif
#endif

#ifdef ENABLED
#ifndef MISSING
int nested_active = 6;
#endif
#endif
