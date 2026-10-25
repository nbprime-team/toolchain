/* stdio.h —— HP Prime 移植自带的 stdio 头（遮蔽 newlib 的同名头）。
 *
 * TCC 的源码按**本头**编译：FILE/stdout/stderr 指向我们自己的 HP_FILE
 * 对象（见 hp_libc.h），于是 fprintf(stderr, ...) 直接写进 PRIMELOG 环形缓冲，
 * 而不经过 newlib 的可重入宏。
 */
#ifndef _HP_STDIO_H
#define _HP_STDIO_H

/* size_t：与 prime.h / hp_string.h / hp_sys.h 共用同一 guard，避免重复定义。 */
#ifndef PRIME_SIZE_T_DEFINED
#define PRIME_SIZE_T_DEFINED
#define SIZE_T_DEFINED
#if defined(__SIZE_TYPE__)
typedef __SIZE_TYPE__ size_t;
#else
typedef unsigned int size_t;
#endif
#endif
typedef __builtin_va_list va_list;
#define NULL ((void *)0)
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

struct HP_FILE;
typedef struct HP_FILE FILE;

extern FILE __hp_stdout;
extern FILE __hp_stderr;
#define stdin  ((FILE *)0)
#define stdout (&__hp_stdout)
#define stderr (&__hp_stderr)

FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *f);
int   fclose(FILE *f);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int   fseek(FILE *f, long offset, int whence);
long  ftell(FILE *f);
int   feof(FILE *f);
int   fflush(FILE *f);
int   fputs(const char *s, FILE *f);
int   fputc(int c, FILE *f);
int   fgetc(FILE *f);
int   remove(const char *path);
int   rename(const char *old, const char *new);

int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int puts(const char *s);
int putchar(int c);

#endif
