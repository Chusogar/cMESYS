#ifndef ANDROID_SHIM_MATH_H
#define ANDROID_SHIM_MATH_H
/* Android's libm (build_apk.sh links these against a stub libm.so) */
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_SQRT2 1.41421356237309504880
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_E 2.7182818284590452354
#define HUGE_VAL __builtin_huge_val()
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
double sin(double); double cos(double); double tan(double); double asin(double); double acos(double);
double atan(double); double atan2(double, double); double sinh(double); double cosh(double); double tanh(double);
double pow(double, double); double exp(double); double exp2(double); double log(double); double log10(double);
double log2(double); double sqrt(double); double floor(double); double ceil(double); double fabs(double);
double fmod(double, double); double round(double); double trunc(double); double rint(double);
double ldexp(double, int); double frexp(double, int *); double modf(double, double *); double hypot(double, double);
long lround(double); long lrint(double); long long llround(double);
float sinf(float); float cosf(float); float tanf(float); float atanf(float); float atan2f(float, float);
float powf(float, float); float expf(float); float exp2f(float); float logf(float); float log10f(float);
float log2f(float); float sqrtf(float); float floorf(float); float ceilf(float); float fabsf(float);
float fmodf(float, float); float roundf(float); float truncf(float); float tanhf(float); float ldexpf(float, int);
long lroundf(float); long lrintf(float);
#endif
