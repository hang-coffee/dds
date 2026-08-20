#ifndef DCC_STDARG_H
#define DCC_STDARG_H

typedef char *va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) ((ap) = (ap))
#define va_copy(dst, src) ((dst) = (src))

#endif
