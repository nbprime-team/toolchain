/*
 * hp_math.c —— 简单数学库（见 hp_math.h）。
 *
 * 自包含的纯 C，只用 double 运算（在 ARM926EJ-S 上经 rt_aeabi.o 走
 * 软浮点）加联合体整数位操作。目标精度约 1e-8。实现为紧凑的
 * Taylor/多项式形式，带范围归约。
 */

#include "hp_math.h"

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

void hp_double_to_str(char *buf, double d, int decimals)
{
    char tmp[24];
    int i, n = 0;
    unsigned long long ip, q;
    double f;

    if (d != d) {                      /* NaN */
        buf[0] = 'N'; buf[1] = 'a'; buf[2] = 'N'; buf[3] = 0;
        return;
    }
    if (d < 0.0) { *buf++ = '-'; d = -d; }
    if (d > 1.0e18) { *buf++ = 'B'; *buf++ = 'I'; *buf++ = 'G'; *buf = 0; return; }

    if (decimals > 0) {                 /* 四舍五入 */
        double scale = 1.0;
        int k;
        for (k = 0; k < decimals; k++)
            scale *= 10.0;
        d += 0.5 / scale;
    }
    ip = (unsigned long long)d;        /* 整数部分 */
    f = d - (double)ip;

    /* 打印整数部分（逆序取数字） */
    do {
        tmp[n++] = (char)('0' + (ip % 10));
        q = ip / 10;
        ip = q;
    } while (ip != 0);
    while (n > 0)
        *buf++ = tmp[--n];

    if (decimals > 0) {
        *buf++ = '.';
        for (i = 0; i < decimals; i++) {
            f *= 10.0;
            int digit = (int)f;
            *buf++ = (char)('0' + digit);
            f -= (double)digit;
        }
    }
    *buf = 0;
}
