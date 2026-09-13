/*
 * hp_input.c —— HP Prime G1 上 TCC 编译程序的键盘 + 触摸输入，
 * DOOM 移植风格。
 *
 * 为什么用钩子？固件的 get_event（svc 0x1003f）是*阻塞*的，归 OS UI
 * 事件循环所有：从应用线程直接调用会永久阻塞（已硬件验证：demo 在
 * 第一次轮询时冻结）。可用的 PureDOOM 移植通过给固件 API 槽
 * 0x307fbfa0 打一个 8 字节跳板来解决：
 *
 *     slot[0] = 0xe51ff004            ; ldr pc, [pc, #-4]
 *     slot[1] = &hp_input_hook
 *
 * OS UI 循环经该槽调用 get_event，因此每个输入事件都先经过
 * hp_input_hook()（运行在 OS 线程上）。钩子用真正的 get_event
 * （svc 0x1003f——它不经过被打补丁的槽，因此不会递归）取原始事件，
 * 并把按键/触摸信息拷进一个小型 SPSC 环，应用线程非阻塞轮询。
 * 退出时还原槽（hp_input_remove），使计算器 UI 之后恢复正常。
 *
 * 事件布局（取自 PureDOOM 的 my_get_event_hook）：
 *   ev->[4]  = 0x00100010 按键 / 15 触摸 / 其它 = 无
 *   key:  [28] = 16 按下 / 0x100000 抬起 ；  [34] = u16 扫描码
 *   touch: [24] = u16 数量；第 i 项在 28+12*i：[0]=u32 类型，
 *          [4]=u16 (0=有效)，[6]=u16 x，[8]=u16 y
 */
#include "hp_input.h"

/* 固件 get_event API 槽（svc 0x1003f），与 PureDOOM 打补丁的是同一个 */
#define HP_FW_EVENT_SLOT 0x307fbfa0u

/* ---- 固件 get_event（rt_svc.o） ---- */
unsigned hp_svc_get_event(unsigned ev);

/* ---- SPSC 环队列（钩子写 head，应用读 tail） ---- */
#define HP_Q_SIZE 64
struct hp_qitem {
    unsigned char  type;    /* 1 = 按键，2 = 触摸 */
    unsigned char  down;    /* 按键：1 按下，0 抬起 */
    unsigned short key;     /* 按键：物理扫描码 */
    unsigned short x, y;    /* 触摸：屏幕坐标 */
};
static struct hp_qitem g_q[HP_Q_SIZE];
static volatile int g_head;          /* 写索引（钩子 / OS 线程） */
static volatile int g_tail;          /* 读索引（应用线程） */
static volatile int g_hooked;        /* 槽当前是否已打补丁 */

static unsigned char g_saved[16];    /* 槽原始字节 */

static int q_push(unsigned char type, unsigned char down,
                  unsigned short key, unsigned short x, unsigned short y)
{
    int h = g_head;
    int n = (h + 1) & (HP_Q_SIZE - 1);
    if (n == g_tail)
        return 0;                     /* 队列满，丢弃 */
    g_q[h].type = type;
    g_q[h].down = down;
    g_q[h].key = key;
    g_q[h].x = x;
    g_q[h].y = y;
    g_head = n;
    return 1;
}

/* 由 OS UI 事件循环（经被打补丁的槽）调用，OS 线程 */
static void hp_input_hook(void *ev)
{
    unsigned char *e = (unsigned char *)ev;
    unsigned type;
    int i, n;
    if (!g_hooked)
        return;
    hp_svc_get_event((unsigned)ev);          /* 真正的 get_event（阻塞） */
    /* 应用线程可能在我们在 get_event 中阻塞时卸下了钩子；碰队列 /
     * 用户内存前重新检查 */
    if (!g_hooked)
        return;
    type = *(unsigned *)(e + 4);
    if (type == 0x00100010u) {               /* 按键 */
        unsigned sub = *(unsigned *)(e + 28);
        q_push(1, sub == 16u ? 1 : 0,
               *(unsigned short *)(e + 34), 0, 0);
    } else if (type == 15u) {                /* 触摸 */
        n = *(unsigned short *)(e + 24);
        if (n > 8)
            n = 8;
        for (i = 0; i < n; i++) {
            unsigned char *p = e + 28 + 12 * i;
            if (*(unsigned short *)(p + 4) != 0)
                continue;                    /* 无效项 */
            q_push(2, 0, 0,
                   *(unsigned short *)(p + 6), *(unsigned short *)(p + 8));
        }
    }
}

int hp_input_install(void)
{
    unsigned *slot = (unsigned *)HP_FW_EVENT_SLOT;
    int i;
    if (g_hooked)
        return 1;
    if (slot[0] == 0xe51ff004u)              /* 已打过补丁 */
        return 0;
    for (i = 0; i < 16; i++)
        g_saved[i] = ((unsigned char *)slot)[i];
    slot[0] = 0xe51ff004u;                   /* ldr pc, [pc, #-4] */
    slot[1] = (unsigned)hp_input_hook;
    g_hooked = 1;
    g_head = 0;
    g_tail = 0;
    return 1;
}

void hp_input_remove(void)
{
    unsigned char *slot = (unsigned char *)HP_FW_EVENT_SLOT;
    int i;
    if (!g_hooked)
        return;
    for (i = 0; i < 16; i++)
        slot[i] = g_saved[i];
    g_hooked = 0;
}

int hp_poll_event(hp_event *ev)
{
    int t, n, i;
    unsigned char *e;
    if (!ev)
        return HP_EV_NONE;
    t = g_tail;
    if (t == g_head)
        return HP_EV_NONE;
    e = (unsigned char *)ev;
    for (i = 0; i < 128; i++)
        e[i] = 0;
    n = (t + 1) & (HP_Q_SIZE - 1);
    if (g_q[t].type == 1) {                  /* 按键 */
        *(unsigned *)(e + 4) = 0x00100010u;
        *(unsigned *)(e + 28) = g_q[t].down ? 16u : 0x100000u;
        *(unsigned short *)(e + 34) = g_q[t].key;
    } else {                                 /* 触摸 */
        *(unsigned *)(e + 4) = 15u;
        *(unsigned short *)(e + 24) = 1;
        *(unsigned *)(e + 28) = 1u;
        *(unsigned short *)(e + 34) = g_q[t].x;
        *(unsigned short *)(e + 36) = g_q[t].y;
    }
    g_tail = n;
    return g_q[t].type == 1 ? HP_EV_KEY : HP_EV_TOUCH;
}

int hp_event_key(const hp_event *ev)
{
    return *(unsigned short *)((char *)ev + 34);
}

int hp_event_key_down(const hp_event *ev)
{
    return *(unsigned *)((char *)ev + 28) == HP_EV_KEY_DOWN;
}

int hp_event_touch_count(const hp_event *ev)
{
    return *(unsigned short *)((char *)ev + 24);
}

int hp_event_touch(const hp_event *ev, int i, int *x, int *y)
{
    char *p = (char *)ev + 28 + 12 * i;
    if (*(unsigned short *)(p + 4) != 0)
        return 0;
    if (x)
        *x = *(unsigned short *)(p + 6);
    if (y)
        *y = *(unsigned short *)(p + 8);
    return *(unsigned *)p;
}
