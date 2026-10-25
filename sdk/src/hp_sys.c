/*
 * hp_sys.c -- 供 TCC 编译程序使用的系统库 (见 hp_sys.h)。
 *
 * 自包含的纯 C, 编译进 rt_core.o。所有调用都经由
 * rt_svc.o 固件封装; 这里唯一的逻辑是 fopen 的 ASCII->UTF-16
 * 转换以及二进制探测式堆大小估算。
 */
#include "hp_sys.h"

/* ---- 固件 SVC 封装 (rt_svc.o) ---- */
unsigned hp_svc_malloc(unsigned n);
unsigned hp_svc_calloc(unsigned n, unsigned sz);
unsigned hp_svc_realloc(unsigned p, unsigned n);
void     hp_svc_free(unsigned p);
void     hp_svc_sleep(unsigned ms);
unsigned hp_svc_getlcd(unsigned dummy);
unsigned hp_svc_get_event(unsigned ev);
unsigned hp_svc_create_thread(unsigned fn, unsigned arg, unsigned stack);
unsigned hp_svc_fopen(unsigned path16, unsigned mode16);
unsigned hp_svc_fclose(unsigned fd);
unsigned hp_svc_fread(unsigned buf, unsigned n, unsigned size, unsigned fd);
unsigned hp_svc_fwrite(unsigned buf, unsigned n, unsigned size, unsigned fd);
unsigned hp_svc_fseek(unsigned fd, unsigned off, unsigned whence);
unsigned hp_svc_ftell(unsigned fd);
unsigned hp_svc_filesize(unsigned fd);

/* ---- 内存 ---- */
void *hp_sys_malloc(size_t n) { return (void *)hp_svc_malloc((unsigned)n); }
void *hp_sys_calloc(size_t n, size_t sz)
{
    return (void *)hp_svc_calloc((unsigned)n, (unsigned)sz);
}
void *hp_sys_realloc(void *p, size_t n)
{
    return (void *)hp_svc_realloc((unsigned)(unsigned long)p, (unsigned)n);
}
void hp_sys_free(void *p) { hp_svc_free((unsigned)(unsigned long)p); }

unsigned long hp_sys_max_alloc(void)
{
    /* 二分搜索固件堆能给出的最大单块
     * (每次探测都 alloc + free; 安全, 从不泄漏)。
     * 注意: hi 上限为 512KB —— 固件在
     * 分配并释放数百 KB 块时的行为尚未完全
     * 经过硬件验证 (一次 4MB 探测在任何绘制之前
     * 就重启了计算器; 见 demo 历史)。512KB 的 alloc/free 处于
     * 已验证范围内 (code.elf 约 230KB 的 free, 约 1.5MB 的加载)。 */
    unsigned long lo = 0, hi = 512u * 1024u, best = 0;
    while (lo <= hi) {
        unsigned long mid = lo + (hi - lo) / 2;
        void *p = hp_sys_malloc(mid ? mid : 1);
        if (p) {
            best = mid;
            hp_sys_free(p);
            lo = mid + 1;
        } else {
            hi = mid ? mid - 1 : 0;
        }
    }
    return best;
}

unsigned long hp_sys_heap_free(void)
{
    return hp_sys_max_alloc();      /* 粗略估算, 已在文档中说明 */
}

/* ---- 系统 ---- */
void hp_sys_sleep(unsigned ms) { hp_svc_sleep(ms); }
unsigned long hp_sys_get_lcd(void) { return hp_svc_getlcd(0); }
int hp_sys_get_event(unsigned ev) { return (int)hp_svc_get_event(ev); }
int hp_sys_thread_create(unsigned fn, unsigned arg, unsigned stack)
{
    return (int)hp_svc_create_thread(fn, arg, stack);
}

/* ---- 调试设备 ---- */
int hp_sys_debug_open(void)
{
    unsigned short p16[16], m16[4];
    int i;
    for (i = 0; "debug"[i] && i < 15; i++)
        p16[i] = (unsigned short)"debug"[i];
    p16[i] = 0;
    m16[0] = 'r'; m16[1] = 'b'; m16[2] = 0;
    return (int)hp_svc_fopen((unsigned)(unsigned long)p16,
                             (unsigned)(unsigned long)m16);
}

/* ---- 文件 IO (ASCII -> UTF-16LE) ---- */
unsigned hp_sys_fopen(const char *ascii_path, const char *ascii_mode)
{
    unsigned short p16[128], m16[8];
    int i;
    for (i = 0; ascii_path[i] && i < 126; i++)
        p16[i] = (unsigned short)ascii_path[i];
    p16[i] = 0;
    for (i = 0; ascii_mode[i] && i < 6; i++)
        m16[i] = (unsigned short)ascii_mode[i];
    m16[i] = 0;
    return hp_svc_fopen((unsigned)(unsigned long)p16,
                        (unsigned)(unsigned long)m16);
}

unsigned hp_sys_fclose(unsigned fd) { return hp_svc_fclose(fd); }
unsigned hp_sys_fread(unsigned buf, unsigned n, unsigned size, unsigned fd)
{
    return hp_svc_fread(buf, n, size, fd);
}
unsigned hp_sys_fwrite(unsigned buf, unsigned n, unsigned size, unsigned fd)
{
    return hp_svc_fwrite(buf, n, size, fd);
}
unsigned hp_sys_fseek(unsigned fd, unsigned off, unsigned whence)
{
    return hp_svc_fseek(fd, off, whence);
}
unsigned hp_sys_ftell(unsigned fd) { return hp_svc_ftell(fd); }
unsigned hp_sys_filesize(unsigned fd) { return hp_svc_filesize(fd); }
