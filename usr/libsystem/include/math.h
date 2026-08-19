#ifndef _MATH_H
#define _MATH_H

#define HUGE_VAL    (__builtin_huge_val())
#define HUGE_VALF   (__builtin_huge_valf())
#define HUGE_VALL   (__builtin_huge_vall())
#define INFINITY    (__builtin_inff())
#define NAN         (__builtin_nanf(""))

#define isnan(x)    __builtin_isnan(x)
#define isinf(x)    __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)

static inline double fabs(double x) { return __builtin_fabs(x); }
static inline float fabsf(float x) { return __builtin_fabsf(x); }
static inline double floor(double x) { return __builtin_floor(x); }
static inline double ceil(double x) { return __builtin_ceil(x); }
static inline double sqrt(double x) { return __builtin_sqrt(x); }
static inline double ldexp(double x, int exp) { return __builtin_ldexp(x, exp); }
static inline long double ldexpl(long double x, int exp) { return __builtin_ldexpl(x, exp); }
static inline double frexp(double x, int *exp) { return __builtin_frexp(x, exp); }

#endif
