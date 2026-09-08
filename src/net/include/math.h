#ifndef ARCHA_MATH_H
#define ARCHA_MATH_H

#include "../js/js_shim.h"

#define M_PI   3.14159265358979323846
#define M_PI_2 1.57079632679489661923

static inline float fabsf(float x) { return (float)fabs((double)x); }
static inline float sqrtf(float x) { return (float)sqrt((double)x); }
static inline float sinf(float x) { return (float)sin((double)x); }
static inline float cosf(float x) { return (float)cos((double)x); }
static inline float tanf(float x) { return (float)tan((double)x); }
static inline float ceilf(float x) { return (float)ceil((double)x); }
static inline float floorf(float x) { return (float)floor((double)x); }
static inline float fmodf(float x, float y) { return (float)fmod((double)x, (double)y); }
static inline float acosf(float x) { return (float)acos((double)x); }
static inline float asinf(float x) { return (float)asin((double)x); }
static inline float atanf(float x) { return (float)atan((double)x); }
static inline float atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }
static inline float powf(float x, float y) { return (float)pow((double)x, (double)y); }
static inline float expf(float x) { return (float)exp((double)x); }
static inline float logf(float x) { return (float)log((double)x); }
static inline float roundf(float x) { return (float)round((double)x); }

/* ldexp: x * 2^exp — used by stb_vorbis for Vorbis codebook floor decoding */
static inline double ldexp(double x, int exp) {
    double factor = 1.0;
    if (exp > 0) {
        while (exp--) factor *= 2.0;
    } else if (exp < 0) {
        while (exp++) factor /= 2.0;
    }
    return x * factor;
}

#endif /* ARCHA_MATH_H */
