/*
 * hp_rt.c -- 在 HP Prime G1 上，供 TCC 编译的用户程序使用的 runtime。
 *
 * 本文件由 TCC 本身在计算器上编译，连同用户的 code.c 一起。它提供：
 *   - printf(fmt, ...) / sprintf：格式化输出到 PRIMELOG ring。
 *     TCC 的 ARM 后端没有 varargs 支持，所以参数个数固定为 4
 *     （未使用的传 0）。也提供便利 helper prints/printd/printx。
 *   - malloc / free（通过 rt_svc.o wrapper 使用固件 heap）
 *   - string/memory helper
 *   - __sleep(ms) 以及通过 hp_svc_*（rt_svc.o）直接访问固件
 *
 * 自包含的纯 C，这样 TCC 无需任何 header 即可编译；
 * 用户代码应 `#include "prime.h"` 以获取声明。
 */

/* ---- 类型 ---- */
#ifndef SIZE_T_DEFINED
#define SIZE_T_DEFINED
typedef unsigned int size_t;
#endif
#define NULL ((void *)0)

#include "hp_icall.h"

/* ---- 固件 SVC wrapper，由 rt_svc.o（预编译）提供 ---- */
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

/* ---- PRIMELOG ring：main.py 通过扫描 "PRIMELOG" 找到它 ---- */
struct hp_log_ring { char magic[8]; volatile int count; char data[32768]; };
static struct hp_log_ring hp_log = { "PRIMELOG", 0, {0} };

/* ---- 持久化诊断：crash.log -----------------------------------------
 * 计算器 console 每次重启都会清空，而 MicroPython 侧的
 * handle 在机器运行中途挂掉时永远不会被 flush —— 所以一次 crash
 * 会悄无声息地丢失所有尚未 fclose() 的内容。因此诊断写入
 * C:\DATA\primetcc.hpappdir\crash.log，采用持久化 append：每
 * 一行都重新读取旧内容（rb）、append、重写（wb+），并在返回
 * 前 CLOSE，这样痕迹能挺过紧接着一条指令之后的瞬间 reset。
 * 内容上限为最后 DIAG_CAP 个 byte，因此文件不会在多次运行中
 * 无限制增长。
 * main.py 在非正常重启后显示此文件（"last crash.log
 * step"），并在正常运行时 / 用户明确同意时清除它。 */
#define DIAG_CAP 8192
static char diag_buf[DIAG_CAP];

static void utf16le(const char *s, unsigned short *out, int cap)
{
    int i;
    for (i = 0; s[i] && i < cap - 1; i++)
        out[i] = (unsigned short)s[i];
    out[i] = 0;
}

void diag_log(const char *s)
{
    unsigned short p16[64], m16[8];
    unsigned have = 0, n = 0, i, drop, fd;

    utf16le("C:\\DATA\\primetcc.hpappdir\\crash.log", p16, 64);
    utf16le("rb", m16, 8);
    fd = hp_svc_fopen((unsigned)(unsigned long)p16, (unsigned)(unsigned long)m16);
    if (fd) {
        have = hp_svc_fread((unsigned)(unsigned long)diag_buf, 1,
                            DIAG_CAP - 512, fd);
        if (have > DIAG_CAP - 512)
            have = DIAG_CAP - 512;
        hp_svc_fclose(fd);
    }
    while (s[n])
        n++;
    drop = 0;
    if (have + n > DIAG_CAP - 512)
        drop = have + n - (DIAG_CAP - 512);
    if (drop >= have) {
        have = 0;
    } else {
        for (i = 0; i < have - drop; i++)   /* 保留最新的 byte */
            diag_buf[i] = diag_buf[i + drop];
        have -= drop;
    }
    for (i = 0; i < n; i++)
        diag_buf[have + i] = s[i];
    have += n;
    utf16le("wb+", m16, 8);
    fd = hp_svc_fopen((unsigned)(unsigned long)p16, (unsigned)(unsigned long)m16);
    if (fd) {
        hp_svc_fwrite((unsigned)(unsigned long)diag_buf, 1, have, fd);
        hp_svc_fclose(fd);          /* 已 close == 跨 reset 持久 */
    }
}

static void hp_log_write(const char *s)
{
    while (*s) {
        hp_log.data[hp_log.count & 32767] = *s++;
        hp_log.count++;
    }
}

/* ---- printf/sprintf 支持 ---- */

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

/* 参数游标：转换按顺序消耗 a0..a7 */
static unsigned long next_arg(unsigned long *ap, int *ai)
{
    unsigned long v = ap[*ai];
    if (*ai < 7) (*ai)++;
    return v;
}

static int hp_vfmt(char *buf, int size, const char *fmt,
                   unsigned long a0, unsigned long a1, unsigned long a2,
                   unsigned long a3, unsigned long a4, unsigned long a5,
                   unsigned long a6, unsigned long a7)
{
    char *p = buf;
    int room = size;
    int total = 0;
    unsigned long ap[8] = { a0, a1, a2, a3, a4, a5, a6, a7 };
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

/* prime.h 中 variadic printf/sprintf 宏背后的固定参数个数引擎
 * （TCC-ARM 没有 AAPCS variadic 调用，所以宏根据参数个数分派
 * 到这些 8 参数的 entry point）。 */
int hp_printf8(const char *fmt, unsigned long a0, unsigned long a1,
               unsigned long a2, unsigned long a3, unsigned long a4,
               unsigned long a5, unsigned long a6, unsigned long a7)
{
    char buf[512];
    int n = hp_vfmt(buf, sizeof buf, fmt, a0, a1, a2, a3, a4, a5, a6, a7);
    hp_log_write(buf);
    return n;
}

int hp_sprintf8(char *buf, const char *fmt, unsigned long a0,
                unsigned long a1, unsigned long a2, unsigned long a3,
                unsigned long a4, unsigned long a5, unsigned long a6,
                unsigned long a7)
{
    return hp_vfmt(buf, 1000000, fmt, a0, a1, a2, a3, a4, a5, a6, a7);
}

/* 向后兼容 alias：针对旧的 4 参数版 prime.h 编译的代码
 * 直接调用 printf(fmt, a0, a1, a2, a3) / sprintf(buf, fmt, ...)。
 * 保留这些符号可让旧 rt_core.o 时代的程序（或 code.c 用旧 header
 * 编译的计算器）无需改动即可链接并在新 runtime 上运行。 */
int printf(const char *fmt, unsigned long a0, unsigned long a1,
           unsigned long a2, unsigned long a3)
{
    return hp_printf8(fmt, a0, a1, a2, a3, 0, 0, 0, 0);
}

int sprintf(char *buf, const char *fmt, unsigned long a0, unsigned long a1,
            unsigned long a2, unsigned long a3)
{
    return hp_sprintf8(buf, fmt, a0, a1, a2, a3, 0, 0, 0, 0);
}

int puts(const char *s)
{
    hp_log_write(s);
    hp_log_write("\n");
    return 1;
}

/* 便利 helper */
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

/* ---- memory / string ----
 * 8-byte 对齐 wrapper（与 hp_libc 原因相同：libgcc 的 aeabi helper
 * 对 8-byte 值使用 LDRD/STRD；非对齐访问会在 ARM926EJ-S 上 fault）。 */
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

/* EABI alias：TCC 将 memcpy/memset/memmove 调用映射到 __aeabi_mem* */
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
/* AEABI 的顺序是 (dest, n, c)；C memset 是 (s, c, n) —— 要交换！ */
void *__aeabi_memset(void *d, unsigned n, int c) { return memset(d, c, n); }
void *__aeabi_memset4(void *d, unsigned n, int c) { return memset(d, c, n); }
void *__aeabi_memset8(void *d, unsigned n, int c) { return memset(d, c, n); }
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

/* ---- 杂项用户 helper ---- */
void __sleep(unsigned ms) { hp_svc_sleep(ms); }

/* ---- 程序入口 ----
 * TCC 用 -Wl,-e,hp_entry 链接，因此 ELF 入口是 hp_entry，而非 main。
 *
 * 关键：交给入口的 debug-interface SP 仅 4-byte 对齐，但
 * GCC 构建的 runtime（ftoa、hp_double_to_str、openlibm
 * rt_math.o）用 LDRD/STRD 访问 double/long long，这要求
 * 8-byte 对齐的 SP（AAPCS）。ARM926EJ-S 对非对齐的
 * LDRD/STRD 触发 data abort -> 计算器重启（已在硬件上验证：
 * sin/math demo 重启了，纯整数的 ball demo 没有）。所以我们在
 * 调用 main() 前把 SP 对齐到 8，并在返回前恢复原始 SP。
 */
/* ---- 常驻 input hook（hp_input_svc.c，位于 tcc.elf 内） ----
 * main.py 在 r0 中把 PRIMEIN state 地址传给 code.elf 入口；
 * hp_entry 将它存到这里，hp_input.c 通过 accessor 读取它。
 *
 * 注意：g_hp_input 必须是 STATIC。TCC 的 ARM linker 不会为非
 * static（global）数据符号生成 GOT fixup：基于 GOT 的 global
 * 访问其 literal pool 会全是 0，程序便会写到地址 0（已在
 * emulator 上验证）。Static global 使用带 R_ARM_RELATIVE reloc 的
 * PC 相对 literal，TCC 能正确处理 —— 所以跨文件访问走函数，
 * 而不是变量。 */
static unsigned g_hp_input;

void hp_set_input_state(unsigned v, unsigned dbg_sp)
{
    char b[96];
    unsigned sp;
    __asm__("mov %0, sp" : "=r"(sp));
    g_hp_input = v;
    /* 诊断：debug-interface SP 与 program-stack SP 的对比。若在真实
     * 设备上 malloc 出的 program stack 紧贴在 debug stack 下方，
     * 深的 eval frame 可能覆盖 debug stack 持有的内容。 */
    hp_sprintf8(b, "RUN begin: state=%x dbg=%x prog=%x\n",
                v, dbg_sp, sp, 0, 0, 0, 0, 0);
    diag_log(b);
}

unsigned hp_get_input_state(void)
{
    return g_hp_input;
}

void hp_input_remove(void);

/* ---- input hook 拆除（hp_input.c） ----
 * 该 hook 常驻于 tcc.elf（永不释放），所以在这里恢复 slot
 * 只是一种礼节：即便 OS thread 正处于 hook 中，它也会在常驻
 * 代码中醒来，code.elf 始终可以安全释放。无需 drain，无需
 * leak-keep 路径，无需 "RT_EXIT wait" —— 整个退出竞态都消失了。 */
int hp_teardown_input(void)
{
    hp_input_remove();               /* 恢复 slot（若未安装则为 no-op） */
    hp_log_write("RT_EXIT ok\n");
    diag_log("RUN end: teardown ok (slot restored)\n");
    return 1;
}

/* ---- 真实 exit code 上报 ----
 * 固件 debug interface 的调用返回对用户程序入口而言是不可靠的：
 * 在硬件上无论 main() 返回什么它都报 2
 * （已验证：ball demo 返回 0，interface 仍说 2）。所以
 * hp_entry 把真实 exit code 以 "RT_RET:<n>" 记入 PRIMELOG ring，
 * main.py 在那里解析它。（Global，非 static：GCC 不得消除一个
 * 仅被 inline asm 引用的函数。） */
void hp_log_ret(unsigned v)
{
    char b[16], d[10];
    const char *p = "RT_RET:";
    int i = 0, j = 0;
    while (*p) b[i++] = *p++;
    do { d[j++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (j) b[i++] = d[--j];
    b[i++] = '\n';
    b[i] = 0;
    hp_log_write(b);
}

int main(void);

/* hp_entry：code.elf 的 entry point。debug interface 交给我们的
 * 是可能只有 4 对齐的 SP，以及 r0 = PRIMEIN input-state 地址（main.py）。
 * GCC 构建的 runtime（ftoa/openlibm）使用 LDRD/STRD，要求 8-byte
 * 对齐（AAPCS），而 ARM926EJ-S 在非对齐 LDRD/STRD 上 fault ->
 * 重启。对齐 SP、发布 state 地址、运行 main()、恢复原始 SP，
 * 在 r0 中返回 main() 的值。运行在 debug-interface
 * stack 上（保持浅：只有这个 entry frame + main() 自己的 frame）。 */
/* 大 program stack：loader 在小 debug-interface stack 上运行
 * code.elf，该 stack 对于递归 parser 和 openlibm 的 frame 来说太
 * 浅 —— 一次溢出会无声地破坏内存，计算器便"在用 double 后重启"
 * （正是 README 为 math demo 记录的那类 crash）。hp_entry 在
 * 调用 main() 前切换到真正的 stack。
 * 该 stack 从固件 heap MALLOC 而来（一次，惰性），而非 static
 * 数组：static 数组在 TCC 链接中会紧邻用户程序自己的 .bss，
 * stack 随后就会与用户数据重叠（已观察到：demo 的 widget state
 * 被 stack top 覆盖）。 */
#define HP_MAIN_STACK_SIZE 16384

/* 仅被下方的 naked asm 引用：保持它存活。
 * 注意：不要在这里把 malloc 出的 stack 指针缓存进 static —— TCC
 * linker 对混合 GCC/TCC object 的 .bss 布局可能把 runtime static
 * 放到与用户程序 static 相同的地址（已观察到：demo 的 widget
 * cursor 字段与 cache word 撞车，拿到了 stack-top 的值）。改为
 * 每次运行都 malloc，并接受每次运行 16KB 的固件 heap 开销。 */
static unsigned g_prog_top;     /* 程序 stack top，供 hp_exit_trace 使用 */

static unsigned hp_main_stack_top(void) __attribute__((used));
static unsigned hp_main_stack_top(void)
{
    /* +64，而不是 +32：hp_entry 把它的 2-word teardown pair 放在
     * [top]，而使用真实固件 allocator 时 p 只有 4 对齐。base =
     * (p+31) & ~7 落在 [p+24, p+31] 中任意处，所以 top+8 可达
     * p + 39 + SIZE —— 用 +32 的余量会写到 malloc 块之外最多
     * 7 个 byte，无声地破坏 NEXT heap block 的 header。
     * malloc 时没有任何校验：graphics demo 跑得好好的，随后
     * 第一次 free()（ESC 上的 GROB / main.py 中的 code buffer）走过
     * 被破坏的 header，重置了计算器。emulator 的 bump allocator
     * 没有 metadata，所以每个测试仍然通过。 */
    unsigned char *p = (unsigned char *)malloc(HP_MAIN_STACK_SIZE + 64);
    unsigned top = ((unsigned)p + 31) & ~7u;
    g_prog_top = top;
    return top + HP_MAIN_STACK_SIZE;
}

#if defined(__arm__)
static unsigned g_a8;           /* debug-stack save-pair 基址 */

void hp_save_a8(unsigned a8) __attribute__((used));
void hp_save_a8(unsigned a8) { g_a8 = a8; }

unsigned hp_get_a8(void) __attribute__((used));
unsigned hp_get_a8(void) { return g_a8; }

void hp_exit_trace(void) __attribute__((used));
/* 就在 hp_entry 恢复它们之前，dump 出 entry save pair 以及
 * callee-saved 寄存器。设备证据：pair 读回完好、sp 也正确，但
 * 机器却在 'bx lr' 与 Python 之间挂掉（无 M0）—— 但仅限
 * 计算量大的运行。TCC 违反 AAPCS，所以一次计算器运行
 * （递归 parser + 经 TCC 代码的 openlibm）会撕碎 r4-r11；调用
 * 我们的 loader shellcode 信任这些寄存器会被保留，结果死在
 * 垃圾值上。空 main / 轻量 demo 恰好没动它们，于是正常退出。
 * 这一行证明这一点：下次 crash 时观察 r4-r7。 */
void hp_exit_trace(void)
{
    char b[128];
    unsigned sp, r4, r5, r6, r7, a8 = g_a8;
    __asm__("mov %0, sp" : "=r"(sp));
    __asm__("mov %0, r4" : "=r"(r4));
    __asm__("mov %0, r5" : "=r"(r5));
    __asm__("mov %0, r6" : "=r"(r6));
    __asm__("mov %0, r7" : "=r"(r7));
    hp_sprintf8(b, "EXIT: a8=%x ip=%x lr=%x r4=%x r5=%x r6=%x r7=%x sp=%x\n",
                a8, *(unsigned *)(a8 + 32), *(unsigned *)(a8 + 36),
                r4, r5, r6, r7, sp);
    diag_log(b);
}

__attribute__((naked)) int hp_entry(void)
{
    /* AAPCS 边界：loader shellcode 调用 hp_entry 时信任
     * r4-r11 能在调用中存活。TCC 生成的代码会撕碎它们
     * （一次计算器运行会把 openlibm 推进 TCC 编译的 frame 中），
     * 随后机器就在我们的 'bx lr' 与 Python 之间挂掉 —— 而
     * 空 main / 轻量 demo 靠运气存活。所以把所有 callee-
     * saved 寄存器 + ip/lr 保存在 debug stack 上（40 字节），并在
     * 退出时原封不动地交还。由于 TCC 也会破坏 r7，pair 地址
     * 在 main() 期间保存在 g_a8 中。 */
    __asm__ __volatile__(
        "mov ip, sp\n\t"            /* ip = 原始 debug sp */
        "bic sp, sp, #7\n\t"        /* 将 SP 对齐到 8（AAPCS / LDRD） */
        "stmfd sp!, {r4, r5, r6, r7, r8, r9, r10, r11, ip, lr}\n\t"
        "mov r7, sp\n\t"            /* r7 = A-8（pair base） */
        "mov r4, r0\n\t"            /* r4 = PRIMEIN state 地址 */
        "mov r0, r7\n\t"
        "bl hp_save_a8\n\t"         /* g_a8 = pair base（TCC 会破坏 r7） */
        "mov r0, #0\n\t"
        "bl hp_main_stack_top\n\t"  /* r0 = 大 stack top（会记录它） */
        "mov sp, r0\n\t"            /* 切换到 program stack */
        "mov r1, r7\n\t"            /* r1 = debug sp（诊断用） */
        "mov r0, r4\n\t"
        "bl hp_set_input_state\n\t" /* g_hp_input = state 地址 */
        "bl main\n\t"
        "mov r5, r0\n\t"            /* fresh：main() 的返回值 */
        "mov r0, r5\n\t"
        "bl hp_log_ret\n\t"         /* 记录 RT_RET:<n>（真实 exit code） */
        "bl hp_teardown_input\n\t"  /* 恢复 slot（常驻 hook） */
        "bl hp_exit_trace\n\t"      /* 寄存器 + pair，就在恢复之前 */
        "bl hp_get_a8\n\t"          /* r0 = 从内存取回的 pair base */
        "mov sp, r0\n\t"            /* 回到 debug stack 上的 pair */
        "mov r0, r5\n\t"            /* main() 的返回值 */
        "ldmfd sp!, {r4, r5, r6, r7, r8, r9, r10, r11, ip, lr}\n\t"
        "mov sp, ip\n\t"            /* 恢复原始 debug sp */
        "bx lr\n\t");               /* 返回时 AAPCS 寄存器完好 */
}
#else
/* 非 ARM（host 测试）：纯直通 —— 无需 SP 对齐 */
int hp_entry(void)
{
    hp_set_input_state(0);
    return (int)main();
}
#endif


/* ---- qsort / bsearch (stdlib) ----
 * qsort：median-of-three quicksort，对小分区使用 insertion sort
 * （避免经典的二次最坏情况）；通过 char* base 处理任意元素大小。
 * bsearch：教科书式实现。 */
static void swap_bytes(char *a, char *b, size_t n)
{
    while (n--) { char t = *a; *a++ = *b; *b++ = t; }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*cmp)(const void *, const void *))
{
    char *b = (char *)base;
    if (nmemb < 2 || size == 0)
        return;
    if (nmemb <= 7) {
        /* insertion sort */
        size_t i, j;
        for (i = 1; i < nmemb; i++) {
            for (j = i; j > 0 && hp_icall2((void *)cmp,
                (unsigned)(b + (j - 1) * size), (unsigned)(b + j * size)) > 0; j--)
                swap_bytes(b + (j - 1) * size, b + j * size, size);
        }
        return;
    }
    {
        size_t lo = 0, hi = nmemb - 1, mid = nmemb / 2, i, j;
        char *pivot;
        /* 三数取中：b[lo]、b[mid]、b[hi] -> pivot 放到 hi */
        if (hp_icall2((void *)cmp, (unsigned)(b + lo * size),
                      (unsigned)(b + mid * size)) > 0)
            swap_bytes(b + lo * size, b + mid * size, size);
        if (hp_icall2((void *)cmp, (unsigned)(b + lo * size),
                      (unsigned)(b + hi * size)) > 0)
            swap_bytes(b + lo * size, b + hi * size, size);
        if (hp_icall2((void *)cmp, (unsigned)(b + mid * size),
                      (unsigned)(b + hi * size)) > 0)
            swap_bytes(b + mid * size, b + hi * size, size);
        swap_bytes(b + mid * size, b + hi * size, size);
        pivot = b + hi * size;
        i = lo;
        j = hi - 1;
        for (;;) {
            while (i < hi && hp_icall2((void *)cmp, (unsigned)(b + i * size),
                           (unsigned)pivot) <= 0) i++;
            while (j > 0 && hp_icall2((void *)cmp, (unsigned)(b + j * size),
                           (unsigned)pivot) > 0) j--;
            if (i >= j)
                break;
            swap_bytes(b + i * size, b + j * size, size);
        }
        swap_bytes(b + i * size, pivot, size);
        if (i > lo)
            qsort(b, i - lo, size, cmp);              /* 左半部分  */
        if (hi - i > 1)
            qsort(b + (i + 1) * size, hi - i, size, cmp);  /* 右半部分 */
    }
}

void *bsearch(const void *key, const void *base, size_t nmemb,
              size_t size, int (*cmp)(const void *, const void *))
{
    const char *b = (const char *)base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = hp_icall2((void *)cmp, (unsigned)key,
                      (unsigned)(b + mid * size));
        if (c == 0)
            return (void *)(b + mid * size);
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}

int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }
