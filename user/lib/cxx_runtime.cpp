#include <stddef.h>
#include <stdint.h>

extern "C" {
    void *malloc(size_t size);
    void free(void *ptr);
    int printf(const char *fmt, ...);

    typedef void (*func_ptr)(void);
    extern func_ptr __init_array_start[];
    extern func_ptr __init_array_end[];

    void __libc_init_array(void) {
        size_t count = __init_array_end - __init_array_start;
        for (size_t i = 0; i < count; i++) {
            if (__init_array_start[i]) {
                __init_array_start[i]();
            }
        }
    }

    void __cxa_pure_virtual(void) {
        while (1) {}
    }

    int __cxa_atexit(void (*func)(void *), void *arg, void *dso_handle) {
        (void)func; (void)arg; (void)dso_handle;
        return 0;
    }

    void *__dso_handle = 0;

    typedef void (*sighandler_t)(int);
    sighandler_t signal(int signum, sighandler_t handler) {
        (void)signum; (void)handler;
        return 0;
    }

    double sqrt(double x) { return __builtin_sqrt(x); }
    double atan2(double y, double x) {
        double res;
        __asm__ __volatile__("fpatan" : "=t"(res) : "0"(x), "u"(y) : "st(1)");
        return res;
    }
    double sin(double x) {
        double res;
        __asm__ __volatile__("fsin" : "=t"(res) : "0"(x));
        return res;
    }
    double cos(double x) {
        double res;
        __asm__ __volatile__("fcos" : "=t"(res) : "0"(x));
        return res;
    }
    double log(double x) {
        double res;
        __asm__ __volatile__("fldln2; fxch; fyl2x" : "=t"(res) : "0"(x) : "st(1)");
        return res;
    }
    double log10(double x) {
        double res;
        __asm__ __volatile__("fldlg2; fxch; fyl2x" : "=t"(res) : "0"(x) : "st(1)");
        return res;
    }
    double pow(double x, double y) {
        if (y == 0.0) return 1.0;
        if (x == 0.0) return 0.0;
        if (x == 1.0) return 1.0;
        if (y == (double)(int)y && y > 0.0 && y <= 64.0) {
            double r = 1.0;
            int n = (int)y;
            double b = x;
            while (n > 0) {
                if (n & 1) r *= b;
                b *= b;
                n >>= 1;
            }
            return r;
        }
        double res;
        __asm__ __volatile__(
            "fyl2x\n\t"
            "fld %%st(0)\n\t"
            "frndint\n\t"
            "fsubr %%st(0), %%st(1)\n\t"
            "fxch\n\t"
            "f2xm1\n\t"
            "fld1\n\t"
            "faddp\n\t"
            "fscale\n\t"
            "fstp %%st(1)\n\t"
            : "=t"(res)
            : "0"(x), "u"(y)
            : "st(1)"
        );
        return res;
    }
    double floor(double x) { return __builtin_floor(x); }
    double ceil(double x) { return __builtin_ceil(x); }
}

void *operator new(size_t size) {
    printf("[CXX] operator new(%u)\n", (uint32_t)size);
    void *p = malloc(size);
    printf("[CXX] operator new(%u) = %p\n", (uint32_t)size, p);
    return p;
}

void *operator new[](size_t size) {
    printf("[CXX] operator new[](%u)\n", (uint32_t)size);
    void *p = malloc(size);
    printf("[CXX] operator new[](%u) = %p\n", (uint32_t)size, p);
    return p;
}

void operator delete(void *p) noexcept {
    free(p);
}

void operator delete[](void *p) noexcept {
    free(p);
}

void operator delete(void *p, size_t) noexcept {
    free(p);
}

void operator delete[](void *p, size_t) noexcept {
    free(p);
}
