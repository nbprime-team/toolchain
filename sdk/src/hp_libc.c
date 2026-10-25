/*
 * hp_libc.c -- TCC 二进制使用的 HP Prime G1 freestanding 运行时。
 *
 * 在固件 SVC 接口 (hp_svc.s) 之上实现 TCC 所需的 libc 的一小部分。
 * 所有 stdout/stderr 输出都进入 PRIMELOG ring，由 main.py 轮询并在
 * 计算器上打印。
 */
#include "hp_libc.h"

/* ---------------- PRIMELOG ring ---------------- */
struct hp_log_ring __hp_log = { "PRIMELOG", 0, {0} };
static HP_FILE *hp_logf;

void hp_log_write(const char *s)
{
    const char *p = s;
    while (*p) {
        __hp_log.data[__hp_log.count & 32767] = *p++;
        __hp_log.count++;
    }
    /* 尽力而为：持久化到 tcc.log，这样在 crash+reboot 之后仍能
       读回最后的输出（见 main.py） */
#if 1
    if (!hp_logf)
        hp_logf = fopen("C:\\DATA\\primetcc.hpappdir\\tcc.log", "w");
    if (hp_logf && *s) {
        fwrite(s, 1, strlen(s), hp_logf);
        fflush(hp_logf);
    }
#endif
}

/* ---------------- 伪 FILE ---------------- */
HP_FILE __hp_stdout = { -1, 0, -1 };
HP_FILE __hp_stderr = { -1, 0, -1 };

/* ---------------- printf 系列 ---------------- */
int hp_vsnprintf_core(char *buf, size_t size, const char *fmt, va_list ap);

int vprintf(const char *fmt, va_list ap)
{
    char buf[512];
    int n = hp_vsnprintf_core(buf, sizeof buf, fmt, ap);
    hp_log_write(buf);
    return n;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int vfprintf(HP_FILE *f, const char *fmt, va_list ap)
{
    char buf[512];
    int n = hp_vsnprintf_core(buf, sizeof buf, fmt, ap);
    if (f == stdout || f == stderr || f->fd == -1)
        hp_log_write(buf);
    else
        fwrite(buf, 1, n, f);
    return n;
}

int fprintf(HP_FILE *f, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int puts(const char *s)
{
    hp_log_write(s);
    hp_log_write("\n");
    return 1;
}

int putchar(int c)
{
    char s[2];
    s[0] = (char)c;
    s[1] = 0;
    hp_log_write(s);
    return c;
}

/* ---------------- fd 表：POSIX fd <-> 固件句柄 ----------------
 * 固件文件句柄是不透明的值；我们把小的 int 作为 POSIX fd（>= 2）分配出去，
 * 并把映射保存在这里。fd 0/1/2 是伪句柄
 * （stdin / stdout / stderr->PRIMELOG）。 */
#define HP_MAX_FD 64
static unsigned hp_fw[HP_MAX_FD];   /* 每个 fd 对应的固件句柄；0 = 空闲 */

/* fd 0/1/2 保留给 stdin/stdout/stderr（write() 把 1/2 当作
 * 路由到 PRIMELOG ring 的伪句柄），所以真正的文件从 3 开始 */
static int hp_alloc_fd(unsigned fw)
{
    int i;
    for (i = 3; i < HP_MAX_FD; i++)
        if (!hp_fw[i]) {
            hp_fw[i] = fw;
            return i;
        }
    return -1;
}

static unsigned hp_fw_of(int fd)
{
    if (fd >= 2 && fd < HP_MAX_FD)
        return hp_fw[fd];
    return 0;
}

/* ---------------- UTF-16 辅助函数 ---------------- */
static char *utf16dup(const char *s)
{
    int n = 0;
    char *b;
    int i;
    while (s[n]) n++;
    b = malloc(2 * n + 2);
    if (!b)
        return NULL;
    for (i = 0; i < n; i++) {
        b[2 * i] = s[i];
        b[2 * i + 1] = 0;
    }
    b[2 * n] = 0;
    b[2 * n + 1] = 0;
    return b;
}

static const char *mode_from_flags(int flags)
{
    int acc = flags & 3;              /* O_RDONLY=0 O_WRONLY=1 O_RDWR=2 */
    if (acc == 0)
        return "rb";                  /* read："rb"（DOOM loader 验证过的模式） */
    /* 任何 write / append / read-write 访问：固件 fopen 对非只读模式
     * 只接受 "wb+"（已在硬件上通过 gbemu 存档文件验证）："w"、"wb"、
     * "r+" 和 "a" 全部打开失败。"wb+" 会截断，因此 append 和 r+
     * 退化为截断后写入。 */
    return "wb+";
}

/* ---------------- POSIX fd 层 ---------------- */
int open(const char *path, int flags, ...)
{
    char *p16, *m16;
    unsigned fw;
    int fd;
    if (!path)
        return -1;
    p16 = utf16dup(path);
    m16 = utf16dup(mode_from_flags(flags));
    if (!p16 || !m16) {
        if (p16) free(p16);
        if (m16) free(m16);
        return -1;
    }
    fw = hp_svc_fopen((unsigned)p16, (unsigned)m16);
    free(p16);
    free(m16);
    if (!fw)
        return -1;
    fd = hp_alloc_fd(fw);
    if (fd < 0)
        hp_svc_fclose(fw);
    return fd;
}

int close(int fd)
{
    unsigned fw = hp_fw_of(fd);
    if (fd == 1 || fd == 2)
        return 0;
    if (!fw)
        return -1;
    hp_svc_fclose(fw);
    hp_fw[fd] = 0;
    return 0;
}

long read(int fd, void *buf, unsigned long n)
{
    unsigned fw = hp_fw_of(fd);
    if (!fw)
        return -1;
    return (long)hp_svc_fread((unsigned)buf, 1, (unsigned)n, fw);
}

long write(int fd, const void *buf, unsigned long n)
{
    unsigned fw = hp_fw_of(fd);
    if (fd == 1 || fd == 2) {
        /* 分块写入 log ring */
        const char *p = buf;
        unsigned long i;
        for (i = 0; i < n; i += 4096) {
            char tmp[4097];
            unsigned long k, chunk = n - i < 4096 ? n - i : 4096;
            for (k = 0; k < chunk; k++)
                tmp[k] = p[i + k];
            tmp[chunk] = 0;
            hp_log_write(tmp);
        }
        return (long)n;
    }
    if (!fw)
        return -1;
    return (long)hp_svc_fwrite((unsigned)buf, 1, (unsigned)n, fw);
}

long lseek(int fd, long offset, int whence)
{
    unsigned fw = hp_fw_of(fd);
    if (!fw)
        return -1;
    hp_svc_fseek(fw, (unsigned)offset, (unsigned)whence);
    return (long)hp_svc_ftell(fw);
}

int isatty(int fd)
{
    return (fd == 1 || fd == 2) ? 1 : 0;
}

/* ---------------- stdio FILE 层（建立在 fd 表之上） ---------------- */
HP_FILE *fopen(const char *path, const char *mode)
{
    HP_FILE *f;
    int fd;
    int flags;
    if (!path)
        return NULL;
    /* 把 C 模式字符串翻译成 POSIX flags；实际的固件模式字符串
     * 在 open() 内从这些 flags 推导得出（mode_from_flags），
     * 因为固件只接受 "rb" / "wb+" */
    if (mode[0] == 'r') flags = 0;
    else if (mode[0] == 'w') flags = 1 | 0x200;
    else if (mode[0] == 'a') flags = 1;
    else flags = 0;
    if (mode[0] == 'r' && mode[1] == '+') flags = 2;
    fd = open(path, flags);
    if (fd < 0)
        return NULL;
    f = malloc(sizeof *f);
    if (!f) {
        close(fd);
        return NULL;
    }
    f->fd = fd;
    f->pos = 0;
    f->size = -1;
    return f;
}

HP_FILE *fdopen(int fd, const char *mode)
{
    HP_FILE *f;
    (void)mode;
    f = malloc(sizeof *f);
    if (!f)
        return NULL;
    f->fd = fd;
    f->pos = 0;
    f->size = -1;
    return f;
}

HP_FILE *freopen(const char *path, const char *mode, HP_FILE *f)
{
    HP_FILE *nf;
    if (f)
        fclose(f);
    nf = fopen(path, mode);
    return nf;
}

int fclose(HP_FILE *f)
{
    if (!f || f->fd == -1)
        return 0;
    close(f->fd);
    f->fd = -1;
    free(f);
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, HP_FILE *f)
{
    size_t total;
    long got;
    if (!f || f->fd == -1 || size == 0)
        return 0;
    total = size * nmemb;
    got = read(f->fd, ptr, (unsigned long)total);
    if (got <= 0)
        return 0;
    f->pos += got;
    return (size_t)got / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, HP_FILE *f)
{
    size_t total;
    long got;
    if (!f)
        return 0;
    if (f->fd == -1) {
        hp_log_write((const char *)ptr);
        return nmemb;
    }
    total = size * nmemb;
    got = write(f->fd, ptr, (unsigned long)total);
    if (got <= 0)
        return 0;
    f->pos += got;
    return (size_t)got / size;
}

int fseek(HP_FILE *f, long offset, int whence)
{
    long pos;
    if (!f || f->fd == -1)
        return -1;
    pos = lseek(f->fd, offset, whence);
    f->pos = pos;
    return pos < 0 ? -1 : 0;
}

long ftell(HP_FILE *f)
{
    if (!f || f->fd == -1)
        return -1;
    f->pos = lseek(f->fd, 0, SEEK_CUR);
    return f->pos;
}

int feof(HP_FILE *f)
{
    if (!f || f->fd == -1)
        return 0;
    if (f->size < 0) {
        unsigned fw = hp_fw_of(f->fd);
        if (fw)
            f->size = (long)hp_svc_filesize(fw);
    }
    return ftell(f) >= f->size;
}

int fflush(HP_FILE *f)
{
    (void)f;
    return 0;
}

int fputs(const char *s, HP_FILE *f)
{
    if (!f || f->fd == -1) {
        hp_log_write(s);
        return 1;
    }
    return fwrite(s, 1, strlen(s), f) == strlen(s) ? 1 : EOF;
}

int fputc(int c, HP_FILE *f)
{
    char b = (char)c;
    if (!f || f->fd == -1) {
        char s[2];
        s[0] = b;
        s[1] = 0;
        hp_log_write(s);
        return c;
    }
    return fwrite(&b, 1, 1, f) == 1 ? c : EOF;
}

int fgetc(HP_FILE *f)
{
    unsigned char c;
    if (!f || f->fd == -1)
        return EOF;
    if (fread(&c, 1, 1, f) != 1)
        return EOF;
    return c;
}

int remove(const char *path)
{
    (void)path;
    return -1;          /* 固件没有 remove；目前无害 */
}

int rename(const char *oldp, const char *newp)
{
    (void)oldp;
    (void)newp;
    return -1;
}

/* ---------------- memory ----------------
 * 固件 malloc 只保证 4-byte 对齐，但 TCC 的 struct（CType 等）是用
 * LDRD/STRD 访问的 8-byte 类型——未对齐的双字加载会在 ARM926EJ-S 上
 * 触发 trap 并使计算器 crash。因此我们多分配 16，返回 8 字节对齐的
 * 指针，并把 {原始指针, size} 保存在返回指针之前的 8 字节里。
 */
static unsigned hp_align_mem(unsigned raw, size_t n)
{
    unsigned aligned = (raw + 15) & ~7u;   /* >= raw + 8，8 字节对齐 */
    ((unsigned *)aligned)[-2] = raw;
    ((unsigned *)aligned)[-1] = (unsigned)n;
    return aligned;
}

void *malloc(size_t n)
{
    unsigned raw = hp_svc_malloc((unsigned)(n + 16));
    unsigned aligned;
    if (!raw)
        return NULL;
    aligned = hp_align_mem(raw, n);
    /* 清零 payload：固件堆里可能残留陈旧数据，而 TCC
       （像模拟器的全零内存那样）依赖新分配的块读出 0 */
    memset((void *)aligned, 0, n);
    return (void *)aligned;
}

void *calloc(size_t n, size_t sz)
{
    size_t total = n * sz;
    void *p = malloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

void *realloc(void *p, size_t n)
{
    size_t old;
    void *np;
    if (!p)
        return malloc(n);
    old = ((unsigned *)p)[-1];
    np = malloc(n);
    if (!np)
        return NULL;
    memcpy(np, p, old < n ? old : n);
    free(p);
    return np;
}

void free(void *p)
{
    if (p)
        hp_svc_free(((unsigned *)p)[-2]);
}

/* ---------------- 内存/字符串 ---------------- */
void *memcpy(void *d, const void *s, size_t n)
{
    char *dp = d;
    const char *sp = s;
    while (n--)
        *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    char *dp = d;
    const char *sp = s;
    if (dp < sp) {
        while (n--) *dp++ = *sp++;
    } else {
        dp += n;
        sp += n;
        while (n--) *--dp = *--sp;
    }
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
    while (n--) {
        if (*pa != *pb)
            return *pa - *pb;
        pa++;
        pb++;
    }
    return 0;
}

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
    if (n == (size_t)-1)
        return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    char *r = d;
    while (n-- && (*d++ = *s++)) ;
    return r;
}

char *strcat(char *d, const char *s)
{
    char *r = d;
    while (*d) d++;
    while ((*d++ = *s++)) ;
    return r;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    return c == 0 ? (char *)s : NULL;
}

int stricmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c)
            last = s;
        s++;
    }
    if (c == 0)
        return (char *)s;
    return (char *)last;
}

char *strstr(const char *h, const char *n)
{
    if (!*n)
        return (char *)h;
    while (*h) {
        const char *a = h, *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b)
            return (char *)h;
        h++;
    }
    return NULL;
}

/* ---------------- stdlib 类 ---------------- */
long strtol(const char *s, char **end, int base)
{
    long v = 0;
    int sign = 1;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    if (base == 0) {
        base = 10;
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') { base = 16; s += 2; }
            else { base = 8; s++; }
        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    for (;;) {
        int d;
        char c = *s;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (d >= base)
            break;
        v = v * base + d;
        s++;
    }
    if (end)
        *end = (char *)s;
    return sign * v;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    unsigned long v = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '+') s++;
    if (base == 0) {
        base = 10;
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') { base = 16; s += 2; }
            else { base = 8; s++; }
        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    for (;;) {
        int d;
        char c = *s;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (d >= base)
            break;
        v = v * base + d;
        s++;
    }
    if (end)
        *end = (char *)s;
    return v;
}

int atoi(const char *s)
{
    return (int)strtol(s, NULL, 10);
}

/* 简单的十进制/指数 strtod；对 C 字面量来说够用 */
double strtod(const char *s, char **end)
{
    double v = 0.0;
    int sign = 1, esign = 1, exp = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9')
        v = v * 10.0 + (*s++ - '0');
    if (*s == '.') {
        double f = 0.1;
        s++;
        while (*s >= '0' && *s <= '9') {
            v += (*s++ - '0') * f;
            f *= 0.1;
        }
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '-') { esign = -1; s++; }
        else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9')
            exp = exp * 10 + (*s++ - '0');
    }
    if (exp) {
        double e = 10.0;
        int k = exp;
        while (k > 1) { e *= 10.0; k--; }
        if (esign > 0) v *= e; else v /= e;
    }
    if (end)
        *end = (char *)s;
    return sign * v;
}

float strtof(const char *s, char **end)
{
    return (float)strtod(s, end);
}

long double strtold(const char *s, char **end)
{
    return (long double)strtod(s, end);
}

static void qsort_swap(char *a, char *b, size_t sz)
{
    size_t i;
    for (i = 0; i < sz; i++) {
        char t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

void qsort(void *base, size_t n, size_t sz,
           int (*cmp)(const void *, const void *))
{
    char *p = base;
    size_t i, j;
    if (n < 2)
        return;
    /* 简单的插入排序——TCC 只用 qsort 处理小的调试数组 */
    for (i = 1; i < n; i++) {
        for (j = i; j > 0 && cmp(p + j * sz, p + (j - 1) * sz) < 0; j--)
            qsort_swap(p + j * sz, p + (j - 1) * sz, sz);
    }
}

/* ---------------- TCC 链接到的额外 libc 部分 ---------------- */
typedef unsigned long long uint64_t;
typedef long long int64_t;

unsigned long long strtoull(const char *s, char **end, int base)
{
    unsigned long long v = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '+') s++;
    if (base == 0) {
        base = 10;
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') { base = 16; s += 2; }
            else { base = 8; s++; }
        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    for (;;) {
        int d;
        char c = *s;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        s++;
    }
    if (end) *end = (char *)s;
    return v;
}

long long strtoll(const char *s, char **end, int base)
{
    long long v;
    int sign = 1;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    v = (long long)strtoull(s, end, base);
    return sign < 0 ? -v : v;
}

char *strpbrk(const char *s, const char *accept)
{
    while (*s) {
        const char *a = accept;
        while (*a) {
            if (*a == *s)
                return (char *)s;
            a++;
        }
        s++;
    }
    return NULL;
}

char *realpath(const char *path, char *resolved)
{
    size_t n;
    if (!path)
        return NULL;
    n = strlen(path);
    if (resolved) {
        memcpy(resolved, path, n + 1);
        return resolved;
    }
    {
        char *p = malloc(n + 1);
        if (p)
            memcpy(p, path, n + 1);
        return p;
    }
}

long double ldexpl(long double x, int exp)
{
    /* 通过反复翻倍实现 x * 2^exp；在本目标上 ldouble == double */
    double d = (double)x;
    while (exp > 0) { d *= 2.0; exp--; }
    while (exp < 0) { d *= 0.5; exp++; }
    return (long double)d;
}

struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};

struct tm *localtime(const long *t)
{
    static struct tm fixed = {
        0, 0, 12, 1, 0, 126,   /* 2026-01-01 12:00:00 */
        4, 0, 0
    };
    (void)t;
    return &fixed;      /* 为 __DATE__/__TIME__ 固定的日期 (v1) */
}

struct timeval {
    long tv_sec;
    long tv_usec;
};

int gettimeofday(struct timeval *tv, void *tz)
{
    /* 这里不做任何固件调用：svc 0x100A5 ("get_time") 会重启 G1，
     * 而设备没有其他时间源，所以时钟读取固定的 epoch。
     * 这也让 __DATE__/__TIME__ 展开（tccpp.c 会调用 time()）
     * 在计算器上保持安全。 */
    (void)tz;
    if (tv) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
    }
    return 0;
}

int unlink(const char *path)
{
    (void)path;
    return -1;          /* 固件没有 unlink */
}

int sigprocmask(int how, const void *set, void *oset)
{
    (void)how; (void)set; (void)oset;
    return 0;
}

int mprotect(void *addr, unsigned long len, int prot)
{
    (void)addr; (void)len; (void)prot;
    return 0;
}

long sysconf(int name)
{
    if (name == 8)          /* _SC_PAGESIZE（newlib 值） */
        return 4096;
    return -1;
}

char **environ;         /* 计算器上没有环境变量 */

/* ---------------- errno（newlib 头文件期望 __errno()） ------------- */
int errno;

int *__errno(void)
{
    return &errno;
}

char *strerror(int e)
{
    static char b[32];
    snprintf(b, sizeof b, "errno %d", e);
    return b;
}

/* ---------------- exit / 杂项 ---------------- */
static void hp_exit_impl(int code)
{
    hp_exit_code = (unsigned long)code;
    longjmp(hp_exit_env, code);
}

void exit(int code)
{
    hp_exit_impl(code);
}

void abort(void)
{
    hp_log_write("\n[abort]\n");
    hp_exit_impl(1);
}

char *getenv(const char *name)
{
    (void)name;
    return NULL;
}

long time(long *t)
{
    /* 固定 epoch：svc 0x100A5 会重启 G1——见 gettimeofday() */
    long v = 0;
    if (t)
        *t = v;
    return v;
}

int system(const char *cmd)
{
    (void)cmd;
    return -1;
}

int signal(int sig, void (*handler)(int))
{
    (void)sig;
    (void)handler;
    return 0;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *old)
{
    (void)sig;
    (void)act;
    (void)old;
    return 0;
}

char *getcwd(char *buf, size_t n)
{
    (void)n;
    if (buf)
        buf[0] = 0;
    return buf;
}

/* ---------------- 启动：config -> main(argc, argv) ---------------- */
extern int main(int argc, char **argv);

/* 用于在计算器上定位 crash 的一次性文件标记 */
void hp_stage_marker(const char *tag)
{
    HP_FILE *f = fopen("C:\\DATA\\primetcc.hpappdir\\stage.txt", "w");
    if (f) {
        fputs(tag, f);
        fclose(f);
    }
}

static int hp_argc;
static char **hp_argv;

/* 运行在一个固件线程上（512 KB 栈，和 DOOM 游戏线程一样）。
 * main.py 轮询 PRIMELOG ring 并在 TCC_DONE 标记处停止。 */
static void hp_tcc_thread(void)
{
    int code;
    printf("T1:thread started\n");
    hp_stage_marker("S4");   /* 到达这里 = 线程成功启动 */
    code = main(hp_argc, hp_argv);
    printf("T3:main returned %d\n", code);
    printf("TCC_DONE:%d\n", code);
    /* 从线程函数返回即结束该固件线程 */
}

int hp_c_main(void)
{
    struct hp_config *cfg = (struct hp_config *)hp_config_ptr;

    hp_argc = 0;
    hp_argv = NULL;
    hp_stage_marker("S2");
    if (cfg && cfg->magic == HP_CONFIG_MAGIC) {
        hp_argc = cfg->argc;
        hp_argv = cfg->argv;
        printf("TCC entry: args=%d\n", hp_argc);
    } else {
        printf("WARNING: bad config magic, running with argc=0\n");
    }
    hp_stage_marker("S3");
    printf("P:creating thread\n");

    /* 在一个固件线程上运行 TCC 并立即返回；main.py 轮询
     * PRIMELOG ring 以获取进度和 TCC_DONE 标记 */
    hp_svc_create_thread((unsigned)hp_tcc_thread, 0, 0x80000);
    printf("P:thread created\n");
    return 0;
}
