/*
 * hp_input_svc.h -- 常驻（RESIDENT）hook 服务（编译进 tcc.elf，见
 * hp_input_svc.c）与 code.elf 一侧客户端（rt/hp_input.c + rt/hp_rt.c）
 * 之间共享的 input-hook state。
 *
 * 该 state 位于 tcc.elf 的 .data 中：tcc.elf 只加载一次并保持常驻，所以
 * hook 代码和这份 state 永不释放。main.py 扫描 tcc.elf 寻找 "PRIMEIN"
 * magic，计算出运行时地址并把它经 r0 传给 code.elf 的入口；hp_entry 把它
 * 存入 g_hp_input。OS 线程的 hook（在 tcc.elf 中）和 app 线程的客户端
 * （在 code.elf 中）都通过该地址操作这个结构体——布局通过这一个头文件
 * 共享（两侧使用同一编译器）。
 */
#ifndef HP_INPUT_SVC_H
#define HP_INPUT_SVC_H

#define HP_INPUT_MAGIC "PRIMEIN"
#define HP_Q_SIZE 64

struct hp_input_state {
    char magic[8];            /* "PRIMEIN"——main.py 靠它找到 state */
    unsigned hook_fn;         /* 常驻 hook 的绝对地址（reloc）*/
    unsigned saved[4];        /* 原始 slot 字节（16），remove 时恢复 */
    unsigned tramp[4];        /* 原始 slot 字节的副本（16）：hook 入口在此
                                 blx，好让真正的 get_event 完全按 firmware
                                 写入的方式运行（gbemu/DOOM 模式）*/
    int      saved_valid;     /* 捕获到真正的原始字节后置 1 */
    volatile int head;        /* 队列写索引（hook / OS 线程）*/
    volatile int tail;        /* 队列读索引（app 线程）*/
    volatile int in_hook;     /* hook 正在 OS 线程上执行 */
    volatile int hook_calls;  /* OS 调用 hook 的次数 */
    volatile int drop_count;  /* armed==0（无程序）期间丢弃的 event 数 */
    volatile int armed;       /* 有程序正在监听（为 0 时丢弃副本）*/
    struct hp_qitem {
        unsigned char type;   /* 1 = key, 2 = touch */
        unsigned char down;   /* key：1 按下，0 释放 */
        unsigned char action; /* touch：firmware item 类型 1=按下，
                               * 2=移动，8=释放（移动不入队）*/
        unsigned short key;   /* key：物理 scan code */
        unsigned short x, y;  /* touch：屏幕坐标 */
    } q[HP_Q_SIZE];
};

#endif /* HP_INPUT_SVC_H */
