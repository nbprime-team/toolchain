/*
 * app_template.c — HP Prime 原生程序骨架 (freestanding)
 * =======================================================
 * 配合 hp-prime-devkit/generic_loader.py 使用。
 *
 * 本文件是"你能在计算器上跑的 C 程序"的最小完整骨架：
 *   - 入口签名与引导器约定一致:  void* app_entry(BootConfig* cfg)
 *   - 日志环形缓冲 (LogStruct)    — Python 端轮询回显
 *   - BootConfig 结构体布局       — 与 Python 端 struct.pack 一致
 *   - 不依赖 libc                — 裸 ELF, 固件里没有标准 ABI
 *
 * 编译: make && make check
 * 详见 README.md。
 */

/* ============ ABI 约定 (与 generic_loader.py 严格一致) ============ */
#define BOOT_MAGIC         0xD00BC760u   /* Python 端 spec["magic"]        */
#define LOG_BUFFER_SIZE    4096u         /* Python 端 spec["log_buffer_size"] */

typedef struct {
    char *name;
    char *val;
} EnvPair;

typedef struct {
    unsigned short hp_scan;   /* HP Prime 物理键扫描码       */
    unsigned short app_key;   /* 你的程序内部键 ID (自行定义) */
} KeyMap;

/* 28 字节布局 → 现为 32 字节, 顺序与 Python 端 struct.pack("<IIIIIIII", ...) 一致 */
typedef struct {
    unsigned int  magic;          /* 0xD00BC760 */
    unsigned int  argc;
    char        **argv;
    unsigned int  env_count;
    EnvPair      *envs;
    unsigned int  key_count;
    KeyMap       *keys;
    unsigned int  services;       /* PrimeServices* 地址, 0 = 未提供 */
} BootConfig;

/*
 * PrimeServices — 系统服务表 (引导器经 cfg->services 传入, 布局与
 * 移植工程 hp-prime-nmarkdown/port/prime_sys.h 一致):
 *   [0] void* (*malloc)(unsigned)    固件后门 malloc
 *   [1] void  (*free)(void*)
 *   [2] unsigned framebuffer         LCD framebuffer 物理地址 (0=未配置)
 *   [3] unsigned fb_width            (320)
 *   [4] unsigned fb_height           (240)
 *   [5] unsigned fb_stride_bytes     (640)
 *   [6] unsigned (*clock_ms)(void)   毫秒时钟 (0=不可用)
 *   [7] unsigned (*key_read)(void)   读物理键扫描码 (0=无键/未实现)
 */
typedef struct {
    void *(*malloc)(unsigned int);
    void  (*free)(void *);
    unsigned int framebuffer;
    unsigned int fb_width;
    unsigned int fb_height;
    unsigned int fb_stride_bytes;
    unsigned int (*clock_ms)(void);
    unsigned int (*key_read)(void);
} PrimeServices;

/* 日志结构体: 首个成员必须是指向"写指针"的 head */
typedef struct {
    unsigned int head;                 /* 单调递增写指针, % SIZE 为下标 */
    char buffer[LOG_BUFFER_SIZE];
} LogStruct;

static LogStruct g_log;

/* ============ 迷你 libc (避免依赖) ============ */
static void *mini_memcpy(void *dst, const void *src, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static unsigned int mini_strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

/* 环形日志写入口: C 端唯一输出通道 */
static void log_write(const char *s, unsigned int len)
{
    unsigned int i;
    for (i = 0; i < len; i++) {
        g_log.buffer[g_log.head % LOG_BUFFER_SIZE] = s[i];
        g_log.head++;                       /* 溢出自动回绕, uint32 单调递增 */
    }
}

static void log_printf(const char *fmt, unsigned int v)
{
    /* 简易实现: 只支持纯文本 + 一个 %u / %x。够调试用, 复杂格式化自行扩展。 */
    char buf[64];
    unsigned int n = 0, i;
    for (i = 0; fmt[i]; i++) {
        if (fmt[i] == '%') {
            i++;
            if (fmt[i] == 'u') {
                /* 十进制 */
                char tmp[12];
                unsigned int t = v, tn = 0;
                if (t == 0) tmp[tn++] = '0';
                while (t) { tmp[tn++] = '0' + (t % 10); t /= 10; }
                while (tn) buf[n++] = tmp[--tn];
            } else if (fmt[i] == 'x') {
                /* 十六进制 */
                char tmp[12];
                unsigned int t = v, tn = 0;
                if (t == 0) tmp[tn++] = '0';
                while (t) { unsigned int d = t & 0xF;
                            tmp[tn++] = d < 10 ? '0' + d : 'a' + d - 10; t >>= 4; }
                while (tn) buf[n++] = tmp[--tn];
            }
        } else {
            buf[n++] = fmt[i];
        }
        if (n >= sizeof(buf) - 8) break;
    }
    log_write(buf, n);
}

/* ============ 键盘轮询占位 ============ */
/*
 * 推荐: C 端直接读物理键盘 (实时性好)。
 * 在 HP Prime 上可通过调试后门找到固件键盘 API (查导出表), 或操作
 * 键盘控制器寄存器。DOOM 移植即走此路线, 键位映射在 cfg->keys 里。
 * 这里留空: 你的程序按实际硬件/固件查询方式填充。
 */
static unsigned int poll_key(BootConfig *cfg)
{
    (void)cfg;
    /* TODO: 查询物理键盘, 返回 HP 扫描码; 无键时返回 0。 */
    return 0;
}

/* 把 HP 扫描码映射为你的程序内部键 ID; 未映射返回 0 */
static unsigned int map_key(BootConfig *cfg, unsigned int hp_scan)
{
    unsigned int i;
    for (i = 0; i < cfg->key_count; i++) {
        if (cfg->keys[i].hp_scan == (unsigned short)hp_scan)
            return cfg->keys[i].app_key;
    }
    return 0;
}

/* ============ 应用主逻辑 (替换这里) ============ */
static void demo_main(BootConfig *cfg)
{
    unsigned int tick = 0;

    log_printf("hello from HP Prime native app!\n", 0);
    log_printf("argc=%u\n", cfg->argc);
    if (cfg->argc > 0)
        log_write(cfg->argv[0], mini_strlen(cfg->argv[0]));
    log_write("\n", 1);

    /* 演示主循环: 每秒(忙等)打一行 tick + 轮询按键。
     * 真实程序在此放你的游戏/工具逻辑。 */
    for (;;) {
        unsigned int k = map_key(cfg, poll_key(cfg));
        if (k)
            log_printf("key=%u\n", k);
        log_printf("tick=%u\n", tick++);
        {
            volatile unsigned int i;
            for (i = 0; i < 2000000u; i++) ;   /* 简易忙等 (慢速机器上调小) */
        }
    }
}

/* ============ 入口 ============ */
/*
 * 被引导器这样调用:  R0 = cfg, R1 = 0
 * 返回: LogStruct* 地址 (供 Python 端 LogReader 轮询)
 */
void *app_entry(BootConfig *cfg)
{
    if (cfg == 0 || cfg->magic != BOOT_MAGIC) {
        log_write("bad config magic\n", 17);
        return 0;
    }
    demo_main(cfg);
    return &g_log;   /* 永不返回时这里不会被执行, 保留供简单程序直接退出 */
}
