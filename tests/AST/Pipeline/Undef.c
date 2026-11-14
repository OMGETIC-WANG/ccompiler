#define VALUE 7
#define FEATURE 1

int before_undef = VALUE;

#undef VALUE
#undef FEATURE
#undef NEVER_DEFINED

#ifdef VALUE
int value_still_defined = 0;
#else
int value_removed = 1;
#endif

#ifndef FEATURE
int feature_removed = 2;
#endif

#ifdef NEVER_DEFINED
int undefined_macro_leaked = 0;
#endif
