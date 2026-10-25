/*
 * rt_softfloat.c -- HP Prime 移植版的 soft-float 加/减法。
 *
 * libgcc 的 "_arm_" 优化 soft-float（位于 rt_aeabi.o）对异号操作数
 * 计算有误（已核实：5.0 - 4.0 返回约 5.0，4.794 - 4.0 返回约 4.794）。
 * 我们在此提供自己的 IEEE-754 round-to-nearest-even 实现，并将它们
 * 编入 rt_aeabi.o，以取代
 * _arm_addsubdf3.o / _arm_addsubsf3.o。
 *
 * 位布局（double）：s eeeeeeeeeee mmmmm...（1 位符号，11 位指数，52 位尾数）。
 * Value = (-1)^s * 1.m * 2^(e-1023)；  e=0：subnormal 0.m*2^-1022；
 * e=0x7FF：inf/nan。
 */

typedef union { double d; unsigned long long u; } dbl_t;
typedef union { float f; unsigned u; } flt_t;

/* ---- double 加/减核心：op = 0 加，1 减 ---- */
static double d_addsub(double x, double y, int sub)
{
    dbl_t a, b, r;
    unsigned long long m1, m2, m;
    long long e1, e2;
    int sa, sb, sr;

    a.d = x;
    b.d = y;
    if (sub)
        b.u ^= 0x8000000000000000ULL;   /* 翻转 y 的符号 */

    sa = (a.u >> 63) & 1;
    sb = (b.u >> 63) & 1;
    e1 = (a.u >> 52) & 0x7FF;
    e2 = (b.u >> 52) & 0x7FF;
    m1 = a.u & 0xFFFFFFFFFFFFFULL;
    m2 = b.u & 0xFFFFFFFFFFFFFULL;

    /* NaN / infinity（最小化处理） */
    if (e1 == 0x7FF || e2 == 0x7FF) {
        if (e1 == 0x7FF && m1) { a.u |= 0x8000000000000ULL; return a.d; } /* NaN */
        if (e2 == 0x7FF && m2) { b.u |= 0x8000000000000ULL; return b.d; }
        if (e1 == 0x7FF && e2 == 0x7FF)   /* inf - inf = NaN */
            return 0.0 / 0.0;
        return e1 == 0x7FF ? a.d : b.d;
    }

    /* 零操作数 */
    if (e1 == 0 && m1 == 0) {
        if (e2 == 0 && m2 == 0) {         /* 0 + (-0) = +0 */
            r.u = 0;
            return r.d;
        }
        return b.d;
    }
    if (e2 == 0 && m2 == 0)
        return a.d;

    /* 规格化（包含隐含位） */
    if (e1 == 0) {
        int k = 0;
        while (!(m1 & 0x10000000000000ULL)) { m1 <<= 1; k++; }
        m1 &= 0xFFFFFFFFFFFFFULL;
        e1 = 1 - k;
    } else {
        m1 |= 0x10000000000000ULL;
    }
    if (e2 == 0) {
        int k = 0;
        while (!(m2 & 0x10000000000000ULL)) { m2 <<= 1; k++; }
        m2 &= 0xFFFFFFFFFFFFFULL;
        e2 = 1 - k;
    } else {
        m2 |= 0x10000000000000ULL;
    }

    /* 将较大的指数放入 a */
    if (e1 < e2) {
        long long te = e1; e1 = e2; e2 = te;
        unsigned long long tm = m1; m1 = m2; m2 = tm;
        int ts = sa; sa = sb; sb = ts;
    }

    /* 使用 2 个额外的 guard 位：将 m1 上移，对齐 m2，从被丢弃的
       位中捕获 guard（bit 1）和 sticky（bit 0） */
    m1 <<= 2;
    {
        long long diff = e1 - e2;
        if (diff >= 64) {
            m2 = 1;                               /* 纯 sticky */
        } else if (diff > 0) {
            unsigned long long lost = m2 & ((1ULL << diff) - 1);
            m2 = (m2 >> diff) << 2;               /* 对齐后的尾数 */
            if (lost & (1ULL << (diff - 1)))
                m2 |= 2;                          /* guard 位于 bit 1 */
            if (lost & ((1ULL << (diff - 1)) - 1))
                m2 |= 1;                          /* sticky 位于 bit 0 */
        } else {
            m2 <<= 2;
        }
    }

    if (sa == sb) {
        m = m1 + m2;
        sr = sa;
    } else {
        if (m1 >= m2) {
            m = m1 - m2;
            sr = sa;
        } else {
            m = m2 - m1;
            sr = sb;
        }
        if (m == 0) {                     /* +0 结果 */
            r.u = 0;
            return r.d;
        }
    }

    /* 规格化：将 m 的 MSB 移到 bit 54（缩放后的隐含位） */
    while (m & 0x80000000000000ULL) {     /* 先处理高于 bit 54（进位） */
        m >>= 1;
        e1++;
    }
    while (!(m & 0x40000000000000ULL)) {  /* 低于 bit 54 */
        m <<= 1;
        e1--;
        if (e1 < -1074) {                 /* 下溢 -> 0 */
            r.u = 0;
            return r.d;
        }
    }

    /* round-to-nearest-even：guard = bit 1，sticky = bit 0 */
    {
        unsigned long long g = (m >> 1) & 1;
        unsigned long long s = m & 1;
        unsigned long long lsb = (m >> 2) & 1;
        m >>= 2;
        if (g && (s || lsb))
            m++;
        if (m & 0x20000000000000ULL) {    /* 舍入进位 */
            m >>= 1;
            e1++;
        }
    }

    if (e1 >= 0x7FF) {                    /* 溢出 -> inf */
        r.u = ((unsigned long long)sr << 63) | (0x7FFULL << 52);
        return r.d;
    }
    if (e1 <= 0) {                        /* subnormal 结果 */
        unsigned long long shift = (unsigned long long)(1 - e1);
        if (shift < 52)
            m >>= shift;
        else
            m = 0;
        r.u = ((unsigned long long)sr << 63) | (m & 0xFFFFFFFFFFFFFULL);
        return r.d;
    }

    r.u = ((unsigned long long)sr << 63)
        | ((unsigned long long)e1 << 52)
        | (m & 0xFFFFFFFFFFFFFULL);
    return r.d;
}

double __aeabi_dadd(double a, double b) { return d_addsub(a, b, 0); }
double __aeabi_dsub(double a, double b) { return d_addsub(a, b, 1); }
double __aeabi_drsub(double a, double b) { return d_addsub(b, a, 1); }
double __adddf3(double a, double b) { return d_addsub(a, b, 0); }
double __subdf3(double a, double b) { return d_addsub(a, b, 1); }

/* ---- float 加/减（同一算法，23 位尾数） ---- */
static float f_addsub(float x, float y, int sub)
{
    flt_t a, b, r;
    unsigned m1, m2, m;
    int e1, e2, sa, sb, sr;

    a.f = x;
    b.f = y;
    if (sub)
        b.u ^= 0x80000000u;

    sa = (a.u >> 31) & 1;
    sb = (b.u >> 31) & 1;
    e1 = (a.u >> 23) & 0xFF;
    e2 = (b.u >> 23) & 0xFF;
    m1 = a.u & 0x7FFFFF;
    m2 = b.u & 0x7FFFFF;

    if (e1 == 0xFF || e2 == 0xFF) {
        if (e1 == 0xFF && m1) { a.u |= 0x400000; return a.f; }
        if (e2 == 0xFF && m2) { b.u |= 0x400000; return b.f; }
        if (e1 == 0xFF && e2 == 0xFF)
            return 0.0f / 0.0f;
        return e1 == 0xFF ? a.f : b.f;
    }
    if (e1 == 0 && m1 == 0) {
        if (e2 == 0 && m2 == 0) { r.u = 0; return r.f; }
        return b.f;
    }
    if (e2 == 0 && m2 == 0)
        return a.f;

    if (e1 == 0) {
        int k = 0;
        while (!(m1 & 0x800000u)) { m1 <<= 1; k++; }
        m1 &= 0x7FFFFF;
        e1 = 1 - k;
    } else {
        m1 |= 0x800000u;
    }
    if (e2 == 0) {
        int k = 0;
        while (!(m2 & 0x800000u)) { m2 <<= 1; k++; }
        m2 &= 0x7FFFFF;
        e2 = 1 - k;
    } else {
        m2 |= 0x800000u;
    }

    if (e1 < e2) {
        int te = e1; e1 = e2; e2 = te;
        unsigned tm = m1; m1 = m2; m2 = tm;
        int ts = sa; sa = sb; sb = ts;
    }
    {
        int diff = e1 - e2;
        if (diff > 26) {
            m2 = m2 ? 1 : 0;
        } else if (diff > 0) {
            if (diff <= 23) {
                unsigned lost = m2 & ((1u << diff) - 1);
                m2 = (m2 >> diff) | (lost ? 1 : 0);
            } else {
                m2 = m2 ? 1 : 0;
            }
        }
    }

    if (sa == sb) {
        m = m1 + m2;
        sr = sa;
    } else {
        if (m1 >= m2) { m = m1 - m2; sr = sa; }
        else          { m = m2 - m1; sr = sb; }
        if (m == 0) { r.u = 0; return r.f; }
    }

    while (m & 0x1000000u) {              /* 先处理高于 bit 23 的 MSB（进位） */
        m >>= 1;
        e1++;
    }
    while (!(m & 0x800000u)) {            /* MSB 低于 bit 23 */
        m <<= 1;
        e1--;
        if (e1 < -126) { r.u = 0; return r.f; }
    }
    if (m & 1) {
        m++;
        if (m & 0x1000000u) { m >>= 1; e1++; }
    }
    if (e1 >= 0xFF) {
        r.u = ((unsigned)sr << 31) | (0xFFu << 23);
        return r.f;
    }
    if (e1 <= 0) {
        unsigned shift = (unsigned)(1 - e1);
        m = shift < 23 ? (m >> shift) : 0;
        r.u = ((unsigned)sr << 31) | (m & 0x7FFFFF);
        return r.f;
    }
    r.u = ((unsigned)sr << 31) | ((unsigned)e1 << 23) | (m & 0x7FFFFF);
    return r.f;
}

float __aeabi_fadd(float a, float b) { return f_addsub(a, b, 0); }
float __aeabi_fsub(float a, float b) { return f_addsub(a, b, 1); }
float __aeabi_frsub(float a, float b) { return f_addsub(b, a, 1); }
float __addsf3(float a, float b) { return f_addsub(a, b, 0); }
float __subsf3(float a, float b) { return f_addsub(a, b, 1); }

/* ---- 整数 -> float/double 转换（32 位 int/long） ---- */
static double u32_to_double(unsigned long long m, int neg)
{
    dbl_t r;
    int msb = 0;
    if (m == 0) { r.u = 0; return r.d; }
    while ((m >> (msb + 1)) != 0)
        msb++;
    r.u = ((unsigned long long)(neg ? 1 : 0) << 63)
        | ((unsigned long long)(1023 + msb) << 52)
        | ((m << (52 - msb)) & 0xFFFFFFFFFFFFFULL);
    return r.d;
}

static float u32_to_float(unsigned m, int neg)
{
    flt_t r;
    int msb = 0;
    if (m == 0) { r.u = 0; return r.f; }
    while ((m >> (msb + 1)) != 0)
        msb++;
    r.u = ((unsigned)(neg ? 1 : 0) << 31)
        | ((unsigned)(127 + msb) << 23)
        | ((m << (23 - msb)) & 0x7FFFFF);
    return r.f;
}

double __aeabi_i2d(int x)
{
    unsigned m = (unsigned)x;
    int neg = 0;
    if (x < 0) { neg = 1; m = 0u - m; }
    return u32_to_double(m, neg);
}
double __aeabi_l2d(long x)
{
    unsigned m = (unsigned)x;
    int neg = 0;
    if (x < 0) { neg = 1; m = 0u - m; }
    return u32_to_double(m, neg);
}
double __aeabi_ul2d(unsigned long x)
{
    return u32_to_double((unsigned long long)x, 0);
}
float __aeabi_i2f(int x)
{
    unsigned m = (unsigned)x;
    int neg = 0;
    if (x < 0) { neg = 1; m = 0u - m; }
    return u32_to_float(m, neg);
}
float __aeabi_l2f(long x)
{
    unsigned m = (unsigned)x;
    int neg = 0;
    if (x < 0) { neg = 1; m = 0u - m; }
    return u32_to_float(m, neg);
}
float __aeabi_ul2f(unsigned long x)
{
    return u32_to_float((unsigned)x, 0);
}
float __aeabi_ui2f(unsigned x)
{
    return u32_to_float(x, 0);
}

double __aeabi_ui2d(unsigned x)
{
    return u32_to_double((unsigned long long)x, 0);
}

/* float -> double：扩展尾数和指数 */
double __aeabi_f2d(float x)
{
    flt_t a;
    dbl_t r;
    unsigned long long m;
    int e;
    a.f = x;
    m = a.u & 0x7FFFFF;
    e = (a.u >> 23) & 0xFF;
    if (e == 0xFF) {                      /* inf/nan */
        r.u = ((unsigned long long)(a.u >> 31) << 63)
            | (0x7FFULL << 52) | (m << 29);
        return r.d;
    }
    if (e == 0) {                         /* 零 / subnormal */
        if (m == 0) { r.u = ((unsigned long long)(a.u >> 31) << 63); return r.d; }
        /* 规格化 subnormal */
        while (!(m & 0x800000u)) { m <<= 1; e--; }
        m &= 0x7FFFFF;
        e++;
        r.u = ((unsigned long long)(a.u >> 31) << 63)
            | ((unsigned long long)(e + 1023 - 127) << 52)
            | (m << 29);
        return r.d;
    }
    r.u = ((unsigned long long)(a.u >> 31) << 63)
        | ((unsigned long long)(e + 1023 - 127) << 52)
        | (m << 29);
    return r.d;
}

/* copysign：|x| 取 y 的符号 */
double copysign(double x, double y)
{
    dbl_t a, b;
    a.d = x;
    b.d = y;
    a.u = (a.u & 0x7FFFFFFFFFFFFFFFULL) | (b.u & 0x8000000000000000ULL);
    return a.d;
}
