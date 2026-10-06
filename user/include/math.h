#ifndef _MATH_H
#define _MATH_H

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef PI
#define PI 3.14159265358979323846
#endif

int abs(int n);
static inline double fabs(double x) { return (x < 0.0) ? -x : x; }
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

static inline double sqrt(double x) { return __builtin_sqrt(x); }
static inline double sin(double x) { return __builtin_sin(x); }
static inline double cos(double x) { return __builtin_cos(x); }
static inline double atan2(double y, double x) { return __builtin_atan2(y, x); }
static inline double pow(double x, double y) { return __builtin_pow(x, y); }
static inline double log(double x) { return __builtin_log(x); }
static inline double log10(double x) { return __builtin_log10(x); }
static inline double floor(double x) { return __builtin_floor(x); }
static inline double ceil(double x) { return __builtin_ceil(x); }

#endif /* _MATH_H */
