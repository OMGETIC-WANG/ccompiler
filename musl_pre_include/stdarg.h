// Replace stdarg.h, should be in include path prior then crt include

#ifndef _VAARGS_H
#define _VAARGS_H

#include <bits/alltypes.h>

// va_list has defined by bits/alltypes.h in musl

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

#define va_copy(dest, src) ((dest) = (src))

#endif /* _VAARGS_H */
