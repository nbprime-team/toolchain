/*
 * hp_rt.c —— HP Prime G1 上 TCC 编译用户程序的运行时。
 *
 * 本文件由计算器上的 TCC 自身编译，与用户的 code.c 一起。它提供：
 *   - printf(fmt, a0, a1, a2, a3) / sprintf：格式化输出到 PRIMELOG 环形
 *     缓冲。TCC 的 ARM 后端不支持变参，因此参数个数固定为 4（不用的
 *     传 0）。另有便捷辅助 prints/printd/printx。
 *   - malloc / free（经 rt_svc.o 包装的固件堆）
 *   - 字符串/内存辅助
 *   - __sleep(ms) 以及经 hp_svc_*（rt_svc.o）的直接固件访问
 *
 * 自包含的纯 C，使 TCC 无需任何头文件即可编译；用户代码应
 * `#include "prime.h"` 获取声明。
 */

/* ---- 类型 ---- */
typedef unsigned int size_t;
#define NULL ((void *)0)

/* ---- 固件 SVC 包装，由 rt_svc.o（预编译）提供 ---- */
unsigned hp_svc_fopen(unsigned path16, unsigned mode16);
unsigned hp_svc_fclose(unsigned fd);
unsigned hp_svc_fseek(unsigned fd, unsigned off, unsigned whence);
unsigned hp_svc_ftell(unsigned fd);
unsigned hp_svc_fread(unsigned buf, unsigned nmemb, unsigned size, unsigned fd);
unsigned hp_svc_fwrite(unsigned buf, unsigned nmemb, unsigned size, unsigned fd);
unsigned hp_svc_filesize(unsigned fd);
unsigned hp_svc_malloc(unsigned n);
unsigned hp_svc_calloc(unsigned n, unsigned sz);
unsigned hp_svc_realloc(unsigned p, unsigned n);
void     hp_svc_free(unsigned p);
void     hp_svc_sleep(unsigned ms);
unsigned hp_svc_gettime(unsigned dummy);

/* ---- PRIMELOG 环形缓冲：main.py 通过扫描 "PRIMELOG" 找到它 ---- */
struct hp_log_ring { char magic[8]; volatile int count; char data[32768]; };
static struct hp_log_ring hp_log = { "PRIMELOG", 0, {0} };

static void hp_log_write(const char *s)
{
    while (*s) {
        hp_log.data[hp_log.count & 32767] = *s++;
        hp_log.count++;
    }
}

/* ---- printf 支持（固定 4 个参数；TCC-ARM 无变参） ---- */

static void pbuf(char **pp, int *room, char c)
{
    if (*room > 1) { **pp = c; (*pp)++; (*room)--; }
}

static void pstr(char **pp, int *room, const char *s, int len)
{
    int i;
    for (i = 0; i < len; i++) pbuf(pp, room, s[i]);
}

static int num_field(char *out, unsigned long v, int base, int upper,
                     char prefix, int width, int left, int zero)
{
    char digits[32];
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int nd = 0, i, len = 0;
    int pre = (prefix != 0) ? 1 : 0;
    int pad;
    if (v == 0) digits[nd++] = '0';
    while (v) { digits[nd++] = dig[v % base]; v /= base; }
    pad = width - nd - pre;
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

/* 参数游标：转换说明符按顺序消费 a0..a3 */
static unsigned long next_arg(unsigned long *ap, int *ai)
{
    unsigned long v = ap[*ai];
    if (*ai < 3) (*ai)++;
    return v;
}

static int hp_vfmt(char *buf, int size, const char *fmt,
                   unsigned long a0, unsigned long a1,
                   unsigned long a2, unsigned long a3)
{
    char *p = buf;
    int room = size;
    int total = 0;
    unsigned long ap[4] = { a0, a1, a2, a3 };
    int ai = 0;

    while (*fmt) {
        if (*fmt != '%') { pbuf(&p, &room, *fmt++); total++; continue; }
        fmt++;
        {
            int left = 0, zero = 0, plus = 0, space = 0;
            for (;;) {
                if (*fmt == '-') { left = 1; fmt++; }
                else if (*fmt == '0') { zero = 1; fmt++; }
                else if (*fmt == '+') { plus = 1; fmt++; }
                else if (*fmt == ' ') { space = 1; fmt++; }
                else break;
            }
            {
                int width = 0;
                while (*fmt >= '0' && *fmt <= '9')
                    width = width * 10 + (*fmt++ - '0');
                while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 't')
                    fmt++;
                {
                    char conv = *fmt++;
                    char field[64];
                    int n, i;
                    switch (conv) {
                    case 'd': case 'i': {
                        long v = (long)next_arg(ap, &ai);
                        char prefix = 0;
                        if (v < 0) { prefix = '-'; v = -v; }
                        else if (plus) prefix = '+';
                        else if (space) prefix = ' ';
                        n = num_field(field, (unsigned long)v, 10, 0, prefix,
                                      width, left, zero);
                        pstr(&p, &room, field, n); total += n;
                        break;
                    }
                    case 'u':
                        n = num_field(field, next_arg(ap, &ai), 10, 0, 0,
                                      width, left, zero);
                        pstr(&p, &room, field, n); total += n;
                        break;
                    case 'x': case 'X':
                        n = num_field(field, next_arg(ap, &ai), 16, conv == 'X',
                                      0, width, left, zero);
                        pstr(&p, &room, field, n); total += n;
                        break;
                    case 'o':
                        n = num_field(field, next_arg(ap, &ai), 8, 0, 0,
                                      width, left, zero);
                        pstr(&p, &room, field, n); total += n;
                        break;
                    case 'c': {
                        char c = (char)next_arg(ap, &ai);
                        pbuf(&p, &room, c); total++;
                        if (!left)
                            for (i = 0; i < width - 1; i++) pbuf(&p, &room, ' ');
                        break;
                    }
                    case 's': {
                        const char *s = (const char *)next_arg(ap, &ai);
                        int len = 0;
                        if (!s) s = "(null)";
                        while (s[len]) len++;
                        total += len;
                        if (!left)
                            for (i = 0; i < width - len; i++) pbuf(&p, &room, ' ');
                        pstr(&p, &room, s, len);
                        break;
                    }
                    case 'p': {
                        unsigned long v = next_arg(ap, &ai);
                        pstr(&p, &room, "0x", 2);
                        n = num_field(field, v, 16, 0, 0, 0, 0, 0);
                        pstr(&p, &room, field, n);
                        total += n + 2;
                        break;
                    }
                    case '%':
                        pbuf(&p, &room, '%'); total++;
                        break;
                    default:
                        pbuf(&p, &room, '%'); pbuf(&p, &room, conv);
                        total += 2;
                        break;
                    }
                }
            }
        }
    }
    if (room > 0) *p = 0;
    return total;
}

/* 最多 4 个实参的 printf：printf(fmt, a0, a1, a2, a3)；不用的传 0 */
int printf(const char *fmt, unsigned long a0, unsigned long a1,
           unsigned long a2, unsigned long a3)
{
    char buf[512];
    int n = hp_vfmt(buf, sizeof buf, fmt, a0, a1, a2, a3);
    hp_log_write(buf);
    return n;
}

int sprintf(char *buf, const char *fmt, unsigned long a0, unsigned long a1,
            unsigned long a2, unsigned long a3)
{
    return hp_vfmt(buf, 1000000, fmt, a0, a1, a2, a3);
}

int puts(const char *s)
{
    hp_log_write(s);
    hp_log_write("\n");
    return 1;
}

/* 便捷辅助 */
static void hp_log_char(char c)
{
    char s[2];
    s[0] = c;
    s[1] = 0;
    hp_log_write(s);
}

void prints(const char *s) { hp_log_write(s); }

void printd(long v)
{
    char b[16];
    int i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) b[i++] = '0';
    while (v) { b[i++] = '0' + v % 10; v /= 10; }
    if (neg) b[i++] = '-';
    while (i) hp_log_char(b[--i]);
}

void printx(unsigned long v)
{
    char tmp[16];
    int i = 0;
    hp_log_write("0x");
    if (v == 0) hp_log_write("0");
    while (v) { tmp[i++] = "0123456789abcdef"[v & 15]; v >>= 4; }
    while (i) hp_log_char(tmp[--i]);
}

/* ---- 内存 / 字符串 ----
 * 8 字节对齐包装（原因同 hp_libc：libgcc 的 aeabi 辅助函数对 8 字节值
 * 使用 LDRD/STRD；ARM926EJ-S 上非对齐访问会 fault）。 */
void *malloc(size_t n)
{
    unsigned raw = hp_svc_malloc(n + 16);
    unsigned aligned;
    if (!raw) return 0;
    aligned = (raw + 15) & ~7u;
    ((unsigned *)aligned)[-2] = raw;
    ((unsigned *)aligned)[-1] = n;
    return (void *)aligned;
}

void *calloc(size_t n, size_t sz)
{
    unsigned char *p = (unsigned char *)malloc(n * sz);
    unsigned long i;
    if (!p) return 0;
    for (i = 0; i < n * sz; i++) p[i] = 0;
    return p;
}

void free(void *p)
{
    if (p) hp_svc_free(((unsigned *)p)[-2]);
}

void *memcpy(void *d, const void *s, size_t n)
{
    char *dp = d; const char *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *pa = a, *pb = b;
    while (n--) { if (*pa != *pb) return *pa - *pb; pa++; pb++; }
    return 0;
}

/* EABI 别名：TCC 把 memcpy/memset/memmove 调用映射到 __aeabi_mem* */
void *__aeabi_memcpy(void *d, const void *s, unsigned n) { return memcpy(d, s, n); }
void *__aeabi_memcpy4(void *d, const void *s, unsigned n) { return memcpy(d, s, n); }
void *__aeabi_memcpy8(void *d, const void *s, unsigned n) { return memcpy(d, s, n); }
void *__aeabi_memmove(void *d, const void *s, unsigned n)
{
    char *dp = d; const char *sp = s;
    if (dp < sp) { while (n--) *dp++ = *sp++; }
    else { dp += n; sp += n; while (n--) *--dp = *--sp; }
    return d;
}
void *__aeabi_memmove4(void *d, const void *s, unsigned n) { return __aeabi_memmove(d, s, n); }
void *__aeabi_memmove8(void *d, const void *s, unsigned n) { return __aeabi_memmove(d, s, n); }
void *__aeabi_memset(void *d, int c, unsigned n) { return memset(d, c, n); }
void *__aeabi_memset4(void *d, int c, unsigned n) { return memset(d, c, n); }
void *__aeabi_memset8(void *d, int c, unsigned n) { return memset(d, c, n); }
void *__aeabi_memclr(void *d, unsigned n) { return memset(d, 0, n); }
void *__aeabi_memclr4(void *d, unsigned n) { return memset(d, 0, n); }
void *__aeabi_memclr8(void *d, unsigned n) { return memset(d, 0, n); }

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n-- && *a && *a == *b) { a++; b++; }
    if (n == (size_t)-1) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}

/* ---- 用户辅助杂项 ---- */
void __sleep(unsigned ms) { hp_svc_sleep(ms); }
unsigned long __gettime(void) { return hp_svc_gettime(0); }
