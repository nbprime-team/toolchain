/*
 * hp_math.c -- 简易数学库（见 hp_math.h）。
 *
 * 自包含的纯 C，仅使用 double 运算（在 ARM926EJ-S 上经由 rt_aeabi.o
 * 使用软浮点），以及通过 union 进行的整数位操作。
 * 精度目标约 1e-8。实现基于紧凑的 Taylor/多项式，
 * 并结合范围缩减。
 */

#include "hp_math.h"
#include "hp_string.h"      /* ftoa：规范的 double->text 格式化函数 */

double hp_ldexp(double x, int e)
{
    union { double d; unsigned long long u; } c;
    long long ex;
    if (x == 0.0)
        return 0.0;
    c.d = x;
    ex = (long long)((c.u >> 52) & 0x7FF) + e;
    if (ex <= 0)
        return 0.0;
    if (ex >= 0x7FF)
        return x > 0.0 ? 1.0e300 : -1.0e300;
    c.u = (c.u & 0x800FFFFFFFFFFFFFULL) | ((unsigned long long)ex << 52);
    return c.d;
}

/* 遗留入口点：与 ftoa/dtoa 作用相同（hp_string.h）；委托给它，以便
 * 只存在一份 double->text 实现（decimals 语义：
 * 小数点后的定点位数，四舍五入到最近值，0..15）。 */
void hp_double_to_str(char *buf, double d, int decimals)
{
    ftoa(d, buf, decimals);
}
