/* stdarg.h - DOCTOR C 子集：可变参数支持 */
#ifndef DCC_STDARG_H
#define DCC_STDARG_H

/* va_list 使用 char* 保存当前可变参数地址 */
typedef char *va_list;

/* va_start 由编译器内建展开：把 ap 设为第一个可变参数地址 */
#define va_start(ap, last) __builtin_va_start(ap, last)

/* va_arg 由编译器内建展开：读取当前参数并前进 sizeof(type) 字节 */
#define va_arg(ap, type) __builtin_va_arg(ap, sizeof(type))

/* 本实现无需清理 */
#define va_end(ap) ((ap) = (ap))

/* 复制 va_list */
#define va_copy(dst, src) ((dst) = (src))

#endif /* DCC_STDARG_H */
