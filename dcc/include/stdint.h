#ifndef _STDINT_H
#define _STDINT_H

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

typedef signed char int_least8_t;
typedef signed short int_least16_t;
typedef signed int int_least32_t;
typedef signed long int_least64_t;
typedef unsigned char uint_least8_t;
typedef unsigned short uint_least16_t;
typedef unsigned int uint_least32_t;
typedef unsigned long uint_least64_t;

typedef signed int int_fast8_t;
typedef signed int int_fast16_t;
typedef signed int int_fast32_t;
typedef signed long int_fast64_t;
typedef unsigned int uint_fast8_t;
typedef unsigned int uint_fast16_t;
typedef unsigned int uint_fast32_t;
typedef unsigned long uint_fast64_t;

typedef signed long intmax_t;
typedef unsigned long uintmax_t;

typedef signed int intptr_t;
typedef unsigned int uintptr_t;

#define INT8_MIN -128
#define INT8_MAX 127
#define UINT8_MAX 255
#define INT16_MIN -32768
#define INT16_MAX 32767
#define UINT16_MAX 65535
#define INT32_MIN -0x80000000
#define INT32_MAX 0x7fffffff
#define UINT32_MAX 0xffffffff
#define INT64_MIN -0x8000000000000000
#define INT64_MAX 0x7FFFFFFFFFFFFFFF
#define UINT64_MAX 0xFFFFFFFFFFFFFFFF
#define INTMAX_MIN -0x8000000000000000
#define INTMAX_MAX 0x7FFFFFFFFFFFFFFF
#define UINTMAX_MAX 0xFFFFFFFFFFFFFFFF

#endif

