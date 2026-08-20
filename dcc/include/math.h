#ifndef _MATH_H
#define _MATH_H

#define HUGE_VAL 0x1.0p2047
#define HUGE_VALF 0x1.0p255f
// 注意：目前对long double支持有限
#define HUGE_VALL 0x1.0p2047
#define INFINITY 0x7f800000
#define NAN 0x7fc00000

#define FP_INFINITE 0
#define FP_NAN 1
#define FP_NORMAL 2
#define FP_ZERO 3

#define M_PI 3.14159265358979323846
#define M_E 2.71828182845904523536
#define M_LOG2E 1.44269504088896340736
#define M_LOG10E 0.43429448190325182765
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_1_PI 0.31830988618379067154
#define M_2_PI 0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2 1.41421356237309504880
#define M_SQRT1_2 0.70710678118654752440

double acos(double x);
double asin(double x);
double atan(double x);
double atan2(double y, double x);
double cos(double x);
double cosh(double x);
double sin(double x);
double sinh(double x);
double tanh(double x);
double exp(double x);
double frexp(double x, int *exponent);
double ldexp(double x, int exponent);
double log(double x);
double log10(double x);
double modf(double x, double *integer);
double pow(double x, double y);
double sqrt(double x);
double ceil(double x);
double fabs(double x);
double floor(double x);
double fmod(double x, double y);

#endif
