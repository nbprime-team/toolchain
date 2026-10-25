/*
 * hp_libc.h -- 用于 HP Prime G1 版 TCC 的最小 freestanding C 运行时。
 *
 * TCC 二进制自身从 C 库需要的一切，都实现在 HP Prime G1 firmware 的 SVC
 * 接口之上（firmware 确切的 syscall 编号以及 push{r0}/push{lr}/svc 约定
 * 见 hp_svc.s）。
 *
 * 本头文件由 arm-none-eabi-gcc 以 -ffreestanding 编译；没有可用的系统
 * libc，所以所有近似标准的类型/声明都放在这里。
 */
#ifndef HP_LIBC_H
#define HP_LIBC_H

/* ---------------- 类型 ---------------- */
typedef unsigned int   size_t;
typedef int            ptrdiff_t;
typedef unsigned int   uintptr_t;
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef signed char    int8_t;
typedef signed short   int16_t;
typedef signed int     int32_t;

#define NULL ((void *)0)
#define EOF  (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* ---------------- FILE 抽象 ----------------
 * 一个真正的 firmware 文件句柄，或下面两个伪句柄之一（fd == -1），它们把
 * 输出重定向到 PRIMELOG ring，好让 main.py 能在计算器上流式输出编译器
 * 消息。
 */
typedef struct HP_FILE {
    int  fd;      /* firmware 文件句柄；-1 == log ring */
    long pos;     /* 缓存的位置 */
    long size;    /* 缓存的大小，-1 = 未知 */
} HP_FILE;

extern HP_FILE __hp_stdout;
extern HP_FILE __hp_stderr;
#define stdout (&__hp_stdout)
#define stderr (&__hp_stderr)

/* ---------------- setjmp/longjmp（asm，AAPCS） ---------------- */
typedef struct { unsigned long r[10]; } jmp_buf; /* r4-r11, sp, lr */
int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

/* ---------------- stdio ---------------- */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

struct HP_FILE *fopen(const char *path, const char *mode);
int   fclose(struct HP_FILE *f);
size_t fread(void *ptr, size_t size, size_t nmemb, struct HP_FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, struct HP_FILE *f);
int   fseek(struct HP_FILE *f, long offset, int whence);
long  ftell(struct HP_FILE *f);
int   feof(struct HP_FILE *f);
int   fflush(struct HP_FILE *f);
int   fputs(const char *s, struct HP_FILE *f);
int   fputc(int c, struct HP_FILE *f);
int   fgetc(struct HP_FILE *f);
int   remove(const char *path);
int   rename(const char *old, const char *new);

/* POSIX 风格的 fd 层（TCC 的 tccelf.c/libtcc.c 用它做文件 IO）。
 * fd 0/1/2 分别是 stdin/stdout/stderr；>= 2 映射到 firmware 文件句柄。*/
int   open(const char *path, int flags, ...);
int   close(int fd);
long  read(int fd, void *buf, unsigned long n);
long  write(int fd, const void *buf, unsigned long n);
long  lseek(int fd, long offset, int whence);
int   isatty(int fd);

int  printf(const char *fmt, ...);
int  fprintf(struct HP_FILE *f, const char *fmt, ...);
int  sprintf(char *buf, const char *fmt, ...);
int  snprintf(char *buf, size_t n, const char *fmt, ...);
int  vprintf(const char *fmt, va_list ap);
int  vfprintf(struct HP_FILE *f, const char *fmt, va_list ap);
int  vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int  puts(const char *s);
int  putchar(int c);

/* ---------------- 内存 ---------------- */
void *malloc(size_t n);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t n);
void  free(void *p);

void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

/* ---------------- 字符串 ---------------- */
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
char  *strcat(char *d, const char *s);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *h, const char *n);

/* ---------------- 类 stdlib ---------------- */
long  strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
double strtod(const char *s, char **end);
float  strtof(const char *s, char **end);
long double strtold(const char *s, char **end);
int    atoi(const char *s);
void   qsort(void *base, size_t n, size_t sz,
             int (*cmp)(const void *, const void *));
void   exit(int code);
void   abort(void);
char  *getenv(const char *name);
long   time(long *t);
int    system(const char *cmd);
int    signal(int sig, void (*handler)(int));
char  *getcwd(char *buf, size_t n);

/* ---------------- PRIMELOG ring（main.py 扫描 image 找它） ----- */
struct hp_log_ring {
    char magic[8];          /* "PRIMELOG" */
    volatile int count;
    char data[32768];
};
extern struct hp_log_ring __hp_log;
void hp_log_write(const char *s);

/* ---------------- config / entry（见 hp_crt0.s） ---------------- */
struct hp_config {
    unsigned magic;         /* 0xD00BC760 */
    int   argc;
    char **argv;
    unsigned env_count;
    char **envs;
};
#define HP_CONFIG_MAGIC 0xD00BC760u

extern jmp_buf hp_exit_env;
extern unsigned long hp_exit_code;
extern unsigned long hp_retaddr;
extern unsigned long hp_config_ptr;
int hp_c_main(void);        /* 解析 config，调用 main(argc, argv) */
void hp_stage_marker(const char *tag);   /* 崩溃定位 marker */

/* ---------------- firmware SVC 包装函数（hp_svc.s） ---------------- */
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
unsigned hp_svc_create_thread(unsigned fn, unsigned arg, unsigned stack);
unsigned hp_svc_getlcd(unsigned dummy);
void     hp_svc_get_event(unsigned ev);   /* 阻塞；填充 event 结构体 */

#endif /* HP_LIBC_H */
