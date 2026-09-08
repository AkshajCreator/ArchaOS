// src/net/tls/src/rand/sysrng.c — ArchaOS hardware entropy PRNG seeder for BearSSL
#include "inner.h"
#include "../../../pit.h"

static inline uint64_t get_rdtsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static int seeder_archaos(const br_prng_class **ctx)
{
    unsigned char tmp[32];
    uint32_t *w = (uint32_t *)tmp;
    for (int i = 0; i < 8; i++) {
        uint64_t tsc = get_rdtsc();
        uint32_t pit = (uint32_t)pit_ticks();
        uint8_t jitter = 0;
        asm volatile("inb $0x40, %0" : "=a"(jitter));
        w[i] = (uint32_t)tsc ^ (uint32_t)(tsc >> 32) ^ (pit << 8) ^ ((uint32_t)jitter << 24) ^ (0x9E3779B9u * (uint32_t)(i + 1));
    }
    (*ctx)->update(ctx, tmp, sizeof(tmp));
    return 1;
}

br_prng_seeder br_prng_seeder_system(const char **name)
{
    if (name != NULL) *name = "archaos_entropy";
    return &seeder_archaos;
}
