#define ENABLED 1
#define DISABLED 0

#if ENABLED
int if_enabled = 1;
#else
int if_enabled_else = 0;
#endif

#if DISABLED
int if_disabled_then = 0;
#else
int if_disabled_else = 2;
#endif

#if 0
#error inactive_error
#warning "inactive warning"
#ifdef ENABLED
int inactive_nested = 0;
#endif
#endif

#if ENABLED
#if !DISABLED
int nested_if_enabled = 3;
#endif
#endif
