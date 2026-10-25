/*
 * hp_input.c -- 供 TCC 编译的程序在 HP Prime G1 上使用的 keyboard + touch
 * input 客户端，DOOM-port 风格。
 *
 * 真正的 hook 常驻（RESIDENT）在 tcc.elf 内部（rt/hp_input_svc.c）：
 * firmware 的 get_event slot（0x307fbfa0）被指向这样一段代码——它只加载
 * 一次，且永不释放；SPSC 队列 + state 与它一起存放在 tcc.elf 的 .data
 * 中。本文件只负责驱动这份共享 state：
 *
 *   - hp_input_install()：捕获真正的原始 slot 字节（仅一次），
 *     强制（FORCE）把 slot 打到常驻 hook 上，并 arm 队列。它会接管一个
 *     已打过补丁的 slot（来自上一次可能已经挂起的运行），而不是拒绝——
 *     正是这种接管让 input 在多次运行之间保持存活。
 *   - hp_poll_event()：读取共享队列（非阻塞）。
 *   - hp_input_remove()：从共享 state 恢复原始 slot 字节并 disarm。
 *     hook 是常驻的，所以即便进行中的 OS 调用也会在有效代码中唤醒；
 *     code.elf 始终可安全释放。
 *
 * state 地址在 code.elf 入口处经 r0 传入：main.py 在 tcc.elf 中找到
 * "PRIMEIN" magic，hp_entry 把它存入 g_hp_input。
 */
#include "hp_input.h"
#include "hp_input_svc.h"

/* firmware 的 get_event API slot（svc 0x1003f），PureDOOM 打的同一个 */
#define HP_FW_EVENT_SLOT 0x307fbfa0u

/* state 地址，由 hp_entry 从 r0 设置；之所以通过一个函数访问，是因为
 * TCC 的 ARM 链接器无法把对全局变量的 GOT 访问重定位 */
unsigned hp_get_input_state(void);

/* console 辅助函数（hp_rt.c）：写入 PRIMELOG ring，由 main.py 流式输出
 * 到屏幕——用于 input-dead 诊断 */
void prints(const char *s);

/* 持久化诊断（hp_rt.c）：追加写入 tcc.log，它能在重启后保留（console 则不行）*/
void diag_log(const char *s);
int hp_sprintf8(char *buf, const char *fmt, unsigned long a0,
                 unsigned long a1, unsigned long a2, unsigned long a3,
                 unsigned long a4, unsigned long a5, unsigned long a6,
                 unsigned long a7);

static struct hp_input_state *istate(void)
{
    return (struct hp_input_state *)hp_get_input_state();
}

/* flush 指令缓存：slot 补丁和常驻 trampoline 都是代码内存。若不这样做，
 * 一条过时的 I-cache 行会让 OS 继续执行旧的字节，直到无关的 UI 活动使其
 * 失效——这就是真机上经典的“按键要等到一次 touch 之后才开始工作”症状
 * （gbemu 的 aggressive_memcpy 执行相同的 MCR）。*/
static void hp_flush_icache(void)
{
    __asm__ __volatile__(
        "mcr p15, 0, %0, c7, c5, 0\n\t"   /* 使 I-cache 失效 */
        "mcr p15, 0, %0, c7, c10, 4\n\t"  /* 数据同步屏障 */
        :: "r"(0) : "memory");
}

int hp_input_install(void)
{
    unsigned *slot = (unsigned *)HP_FW_EVENT_SLOT;
    struct hp_input_state *s = istate();
    char b[96];
    int i;
    if (!s || s->magic[0] != 'P' || !s->hook_fn) {
        hp_sprintf8(b, "install FAIL: state=%p magic=%c hookfn=%08x\n",
                 (unsigned long)(unsigned)s, s ? s->magic[0] : '?',
                 s ? s->hook_fn : 0, 0, 0, 0, 0, 0);
        diag_log(b);
        return 0;
    }
    /* 只捕获一次真正的原始 slot 字节，存放在共享的（常驻）内存中——
     * 绝不要放在每次运行都可能被释放的 image 里 */
    if (!s->saved_valid) {
        unsigned char *p = (unsigned char *)slot;
        for (i = 0; i < 16; i++)
            ((unsigned char *)s->saved)[i] = p[i];
        s->saved_valid = 1;
    }
    /* 构建常驻 trampoline（gbemu/DOOM 模式）：把原始 slot 字节复制进共享
     * state，这样 hook 入口就能完全按 firmware 写入的方式运行真正的
     * get_event */
    {
        unsigned char *p = (unsigned char *)slot;
        for (i = 0; i < 16; i++)
            ((unsigned char *)s->tramp)[i] = p[i];
    }
    /* 强制打补丁：即使上一次（可能已挂起的）运行把自己的 trampoline 留在
     * 这里也要接管——否则那个过时的 hook 会永远饿死我们 */
    slot[0] = 0xe51ff004u;              /* ldr pc, [pc, #-4] */
    slot[1] = s->hook_fn;
    hp_flush_icache();                  /* 使补丁 + tramp 可见 */
    s->armed = 1;
    /* 无条件重置队列：上一次运行可能让 OS 线程阻塞在常驻 hook 内部
     * （in_hook=1）、并让队列停在过时状态——此时若拒绝重置，hook 就会
     * 丢弃每一个新 event，使得计算器看似忽略按键，直到其他输入唤醒 OS
     * 循环。启动时丢失一个进行中的 event，远好过 input 彻底死掉。*/
    s->head = 0;
    s->tail = 0;
    /* 每次运行的诊断计数器：下面的 input-dead watchdog 会观察本次运行期间
     * hook_calls 的增长（OS 必须在约 60 次 poll 内到达 hook，否则 slot
     * 就是过时的，input 卡住了）*/
    s->hook_calls = 0;
    s->drop_count = 0;
    hp_sprintf8(b, "install OK: saved=%d hookfn=%08x slot=%08x %08x\n",
             s->saved_valid, s->hook_fn, slot[0], slot[1], 0, 0, 0, 0);
    diag_log(b);
    hp_sprintf8(b, "  armed=1 calls=%d drops=%d inhook=%d queue=%d\n",
             s->hook_calls, s->drop_count, s->in_hook, s->tail, 0, 0, 0, 0);
    diag_log(b);
    return 1;
}

void hp_input_remove(void)
{
    unsigned char *slot = (unsigned char *)HP_FW_EVENT_SLOT;
    struct hp_input_state *s = istate();
    int i;
    if (!s || !s->saved_valid)
        return;
    if (((unsigned *)slot)[0] == 0xe51ff004u &&
        ((unsigned *)slot)[1] == s->hook_fn) {
        for (i = 0; i < 16; i++)
            slot[i] = ((unsigned char *)s->saved)[i];
        hp_flush_icache();
    }
    s->armed = 0;
}

/* watchdog：若 slot 不再指向常驻 hook（有人覆盖了它，或上一次 remove 被
 * 中断），就重新 arm 它。开销很小：每次调用 2 次读取；从 hp_poll_event
 * 运行它，好让活跃的循环自愈。*/
static void hp_input_watchdog(void)
{
    unsigned *slot = (unsigned *)HP_FW_EVENT_SLOT;
    struct hp_input_state *s = istate();
    if (!s || !s->armed)
        return;
    if (slot[0] == 0xe51ff004u && slot[1] == s->hook_fn)
        return;
    slot[0] = 0xe51ff004u;
    slot[1] = s->hook_fn;
    hp_flush_icache();
}

int hp_input_hooked(void)
{
    struct hp_input_state *s = istate();
    return (s && s->armed) ? 1 : 0;
}

int hp_input_busy(void)
{
    struct hp_input_state *s = istate();
    return s ? s->in_hook : 0;
}

int hp_poll_event(hp_event *ev)
{
    struct hp_input_state *s = istate();
    static int polls;            /* 用于 input-dead watchdog 的 poll 计数器 */
    static int warned;
    static int sampled;
    char b[96];
    int t, n, i;
    unsigned char *e;
    if (!ev || !s)
        return HP_EV_NONE;
    hp_input_watchdog();
    /* 周期性 tcc.log 采样：每约 100 次 poll 记录 queue/hook 活动 */
    if (++sampled >= 100) {
        sampled = 0;
        hp_sprintf8(b, "poll: calls=%d drops=%d inhook=%d qtail=%d\n",
                 s->hook_calls, s->drop_count, s->in_hook, s->tail, 0, 0, 0, 0);
        diag_log(b);
    }
    /* 诊断：若我们已 arm 但 OS 从未调用常驻 hook（轮询约 1s 后
     * hook_calls 仍为 0），说明 OS 的 input 循环没有读取打过补丁的
     * slot——通常是因为上一次构建把它卡住了。恢复 slot，好让计算器自身
     * 的按键至少还能用，并告知此事。*/
    if (s->armed && s->hook_calls == 0) {
        if (++polls >= 600) {           /* 约 10s：给慢速的 OS 循环时间，
                                           好在 install 之后到达 hook */
            if (!warned) {
                warned = 1;
                hp_input_remove();
                prints("INPUT DEAD: OS never called the input hook; "
                       "slot restored. Reset the calculator (hard reboot) "
                       "and re-run.\n");
                diag_log("INPUT DEAD: OS never called the hook; "
                         "slot restored by watchdog\n");
            }
        }
    } else {
        polls = 0;
    }
    t = s->tail;
    if (t == s->head)
        return HP_EV_NONE;
    e = (unsigned char *)ev;
    for (i = 0; i < 128; i++)
        e[i] = 0;
    n = (t + 1) & (HP_Q_SIZE - 1);
    if (s->q[t].type == 1) {             /* key */
        *(unsigned *)(e + 4) = 0x00100010u;
        *(unsigned *)(e + 28) = s->q[t].down ? 16u : 0x100000u;
        *(unsigned short *)(e + 34) = s->q[t].key;
    } else {                             /* touch */
        *(unsigned *)(e + 4) = 15u;
        if (s->q[t].action == 8u) {
            /* release：合成一个 count-0 的 event——hp_gui_handle 恰好在
             * touch_count == 0 时触发被按下的按钮。（在此之前，release
             * 以普通的 count-1 touch 形式到达，于是 release 分支从未运行，
             * 屏幕上的按钮永远无法触发。）*/
            *(unsigned short *)(e + 24) = 0;
        } else {
            *(unsigned short *)(e + 24) = 1;
            *(unsigned *)(e + 28) = s->q[t].action ? s->q[t].action : 1u;
            *(unsigned short *)(e + 34) = s->q[t].x;
            *(unsigned short *)(e + 36) = s->q[t].y;
        }
    }
    s->tail = n;
    return s->q[t].type == 1 ? HP_EV_KEY : HP_EV_TOUCH;
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
