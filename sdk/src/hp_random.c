/*
 * hp_random.c -- PCG32 PRNG + value noise（见 hp_random.h）。
 *
 * 自包含的纯 C（可被 TCC 编译）。PCG32：
 *   state = state * 6364136223846793005 + 1442695040888963407 (odd)
 *   output = 旋转后的 XOR-移位高位。
 * Value noise：对整数格点角做 hash，双线性 + smoothstep。
 */
#include "hp_random.h"

void hp_rng_seed(hp_rng *r, unsigned seed)
{
    /* 从一个固定的奇数 state 开始，并把 seed 混入后推进一次 */
    r->state = 0x853c49e6748fea9bULL ^ ((unsigned long long)seed << 32) ^ seed;
    hp_rng_u32(r);               /* 预热 */
}

unsigned hp_rng_u32(hp_rng *r)
{
    unsigned long long old = r->state;
    unsigned x, rot;
    r->state = old * 6364136223846793005ULL + 1442695040888963407ULL;
    x = (unsigned)(((old >> 18u) ^ old) >> 27u);
    rot = (unsigned)(old >> 59u);
    return (x >> rot) | (x << ((-rot) & 31u));
}

int hp_rng_range(hp_rng *r, int lo, int hi)
{
    unsigned span;
    if (hi < lo) { int t = lo; lo = hi; hi = t; }
    span = (unsigned)(hi - lo) + 1u;
    /* 对 32-bit span 的无拒绝取模 */
    return lo + (int)(hp_rng_u32(r) % span);
}

hp_fx hp_rng_fx(hp_rng *r)
{
    return (hp_fx)(hp_rng_u32(r) >> 16);   /* [0, 65535] ~ [0, 1) */
}

/* ---- value noise ---- */
unsigned hp_noise2_hash(int x, int y, unsigned seed)
{
    unsigned h = (unsigned)x * 0x27d4eb2du + (unsigned)y * 0x165667b1u
               + seed * 0x9e3779b9u + 0x85ebca6bu;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

static hp_fx smooth(hp_fx t)      /* 3t^2 - 2t^3 */
{
    return hp_fx_mul(t, hp_fx_mul(t, hp_fx_sub(hp_fx_int(3),
                       hp_fx_mul(hp_fx_int(2), t))));
}

hp_fx hp_noise2_fx(hp_fx x, hp_fx y, unsigned seed)
{
    int xi = hp_fx_trunc(x);
    int yi = hp_fx_trunc(y);
    hp_fx xf = x - hp_fx_int(xi);
    hp_fx yf = y - hp_fx_int(yi);
    hp_fx u = smooth(xf);
    hp_fx v = smooth(yf);
    hp_fx a, b, c, d, ab, cd;
    a = (hp_fx)(hp_noise2_hash(xi, yi, seed) & 0xFFFFu);        /* [0,1) */
    b = (hp_fx)(hp_noise2_hash(xi + 1, yi, seed) & 0xFFFFu);
    c = (hp_fx)(hp_noise2_hash(xi, yi + 1, seed) & 0xFFFFu);
    d = (hp_fx)(hp_noise2_hash(xi + 1, yi + 1, seed) & 0xFFFFu);
    ab = hp_fx_lerp(a, b, u);
    cd = hp_fx_lerp(c, d, u);
    return hp_fx_lerp(ab, cd, v);
}
