/*
 * hp_vsnprintf.c -- HP Prime 移植版的最小 vsnprintf。
 *
 * 支持：%d %i %u %x %X %o %c %s %p %% 以及标志 '-', '0', '+', ' '
 * 和宽度（十进制）。'l'/'z'/'t'/'h' 长度修饰符会被接受
 * 并忽略（ILP32：long/size_t 与 int 同大小）。返回本应写入的
 * 字符数（不含 NUL），C99 风格。
 */
#include "hp_libc.h"

static void putc_buf(char **pp, size_t *room, char c)
{
    if (*room > 1) {
        **pp = c;
        (*pp)++;
        (*room)--;
    }
    /* 即使缓冲区已满也始终计数该字符 */
}

static void putstr_buf(char **pp, size_t *room, const char *s, int len)
{
    int i;
    for (i = 0; i < len; i++)
        putc_buf(pp, room, s[i]);
}

/* 构造一个数值字段：将 [padding][prefix][digits] 写入 buf；返回 len。 */
static int build_num(char *out, unsigned long v, int base, int upper,
                     char prefix, int width, int left, int zero)
{
    char digits[32];
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int nd = 0, i, len = 0;
    if (v == 0)
        digits[nd++] = '0';
    while (v) {
        digits[nd++] = dig[v % base];
        v /= base;
    }
    /* 将各位数字逆序放入 prefix 之后的位置 */
    int pre = (prefix != 0) ? 1 : 0;
    int pad = width - nd - pre;
    if (pad < 0) pad = 0;
    if (!left) {
        char pc = (zero && !pre) ? '0' : ' ';
        for (i = 0; i < pad; i++) out[len++] = pc;
    }
    if (prefix) out[len++] = prefix;
    for (i = nd - 1; i >= 0; i--) out[len++] = digits[i];
    if (left)
        for (i = 0; i < pad; i++) out[len++] = ' ';
    return len;
}

int hp_vsnprintf_core(char *buf, size_t size, const char *fmt, va_list ap)
{
    char *p = buf;
    size_t room = size;
    int total = 0;

    while (*fmt) {
        if (*fmt != '%') {
            putc_buf(&p, &room, *fmt++);
            total++;
            continue;
        }
        fmt++;
        /* 标志 */
        int left = 0, zero = 0, plus = 0, space = 0;
        for (;;) {
            if (*fmt == '-') { left = 1; fmt++; }
            else if (*fmt == '0') { zero = 1; fmt++; }
            else if (*fmt == '+') { plus = 1; fmt++; }
            else if (*fmt == ' ') { space = 1; fmt++; }
            else break;
        }
        /* 宽度 */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        /* 长度修饰符：忽略（ILP32） */
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 't')
            fmt++;

        char conv = *fmt++;
        char field[64];
        int n, i;

        switch (conv) {
        case 'd':
        case 'i': {
            long v = va_arg(ap, long);
            char prefix = 0;
            if (v < 0) { prefix = '-'; v = -v; }
            else if (plus) prefix = '+';
            else if (space) prefix = ' ';
            n = build_num(field, (unsigned long)v, 10, 0, prefix,
                          width, left, zero);
            putstr_buf(&p, &room, field, n);
            total += n;
            break;
        }
        case 'u':
            n = build_num(field, (unsigned long)va_arg(ap, unsigned long),
                          10, 0, 0, width, left, zero);
            putstr_buf(&p, &room, field, n);
            total += n;
            break;
        case 'x':
        case 'X':
            n = build_num(field, (unsigned long)va_arg(ap, unsigned long),
                          16, conv == 'X', 0, width, left, zero);
            putstr_buf(&p, &room, field, n);
            total += n;
            break;
        case 'o':
            n = build_num(field, (unsigned long)va_arg(ap, unsigned long),
                          8, 0, 0, width, left, zero);
            putstr_buf(&p, &room, field, n);
            total += n;
            break;
        case 'c': {
            char c = (char)va_arg(ap, int);
            putc_buf(&p, &room, c);
            total += 1;
            if (!left)
                for (i = 0; i < width - 1; i++) putc_buf(&p, &room, ' ');
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            int len = 0;
            if (!s) s = "(null)";
            while (s[len]) len++;
            total += len;
            if (!left)
                for (i = 0; i < width - len; i++) putc_buf(&p, &room, ' ');
            putstr_buf(&p, &room, s, len);
            break;
        }
        case 'p': {
            unsigned long v = (unsigned long)va_arg(ap, void *);
            putstr_buf(&p, &room, "0x", 2);
            n = build_num(field, v, 16, 0, 0, 0, 0, 0);
            putstr_buf(&p, &room, field, n);
            total += n + 2;
            break;
        }
        case '%':
            putc_buf(&p, &room, '%');
            total += 1;
            break;
        default:
            putc_buf(&p, &room, '%');
            putc_buf(&p, &room, conv);
            total += 2;
            break;
        }
    }
    if (room > 0)
        *p = '\0';
    return total;
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    return hp_vsnprintf_core(buf, n, fmt, ap);
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = hp_vsnprintf_core(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = hp_vsnprintf_core(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return r;
}
