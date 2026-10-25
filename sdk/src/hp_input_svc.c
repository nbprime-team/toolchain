/*
 * hp_input_svc.c -- primetcc 的常驻（RESIDENT）firmware input-hook
 * 服务，编译进 tcc.elf（见 Makefile：HP_OBJS += obj/hp_input_svc.o）。
 *
 * 为何常驻：firmware 的 get_event slot（0x307fbfa0）绝不能指向可能被释放
 * 的内存。之前的设计把 hook 放在 code.elf 里，而它每次运行后都会被释放。
 * 若程序退出时 OS 的 input 循环正阻塞在 hook 内部，该调用就会在已释放
 * （且可能被复用的）内存中唤醒，并永久破坏 input：下一次运行会画出自己
 * 的屏幕，却永远收不到按键——而且由于 slot 一直指向挂起程序的 hook，
 * 连 Python app 重启都无法恢复。把 hook 移到此处、放进 tcc.elf（只加载
 * 一次、保持常驻），就让 slot 指向的目标变得永生不灭：
 *
 *   * OS 线程只会执行本文件的代码；
 *   * slot 可以自由地重新打补丁或恢复——真正的原始 slot 字节存放在共享
 *     内存（saved[]）中，而不是每次运行的 image 里；
 *   * code.elf 在每次运行后都无条件可安全释放。
 *
 * main.py 通过扫描 tcc.elf 中的 "PRIMEIN" 定位 state，并把它运行时的地址
 * 经 r0 传给 code.elf 的入口；code.elf 一侧的客户端（rt/hp_input.c）把
 * slot 打到 state->hook_fn，并读取队列。
 */
#include "hp_libc.h"
#include "hp_input_svc.h"

/* firmware 的 get_event API slot，PureDOOM port 打的同一个 */
#define HP_FW_EVENT_SLOT 0x307fbfa0u

/* 共享 state；main.py 通过 magic 找到它（见 hp_input_svc.h）。
 * hook_fn 会得到一个 R_ARM_RELATIVE 重定位，由 ELF loader 修正为 hook
 * 入口的绝对运行时地址。*/
static void hp_input_hook_entry(void *ev);
struct hp_input_state __hp_input = {
    HP_INPUT_MAGIC, (unsigned)hp_input_hook_entry,
    {0}, {0}, 0, 0, 0, 0, 0, 0, 0, {{0}}
};

static void push_item(struct hp_input_state *st, unsigned char type,
                      unsigned char down, unsigned short key,
                      unsigned short x, unsigned short y, unsigned char act)
{
    int h = st->head;
    int n = (h + 1) & (HP_Q_SIZE - 1);
    if (n == st->tail)
        return;                             /* 队列已满，丢弃 */
    st->q[h].type = type;
    st->q[h].down = down;
    st->q[h].action = act;
    st->q[h].key = key;
    st->q[h].x = x;
    st->q[h].y = y;
    st->head = n;
}

/* hook 入口是一个很小的 naked trampoline：通过原始 slot 字节（在 install
 * 时复制到 st->tramp）运行真正的 get_event，然后在 C 中处理 event。经其
 * 原始代码调用 get_event——而不是裸 svc——与真机上可用的 gbemu/DOOM
 * port 一致。*/
static void hp_input_hook_process(void *ev);

/* struct hp_input_state 布局：magic[8] + hook_fn(4) + saved[16] => 28 */
#define HP_TRAMP_OFF 28

__attribute__((naked, noinline)) static void hp_input_hook_entry(void *ev)
{
    __asm__ __volatile__(
        "push {r4, r5, lr}\n\t"
        "mov r4, r0\n\t"                   /* r4 = ev（callee-saved）*/
        "ldr r5, =__hp_input\n\t"
        "add r5, r5, #" /* HP_TRAMP_OFF */ "28\n\t"  /* r5 = tramp */
        "blx r5\n\t"                       /* r0 = ev 保持不变：真正的
                                                get_event(ev) 运行的是
                                                原始 slot 代码 */
        "mov r0, r4\n\t"
        "bl hp_input_hook_process\n\t"
        "pop {r4, r5, pc}\n\t");
}

/* 仅被下面的 naked asm 引用：让它保持存活 */
static void hp_input_hook_process(void *ev) __attribute__((used));
static void hp_input_hook_process(void *ev)
{
    struct hp_input_state *st = &__hp_input;
    unsigned char *e = (unsigned char *)ev;
    unsigned type;
    int i, n;

    st->in_hook++;
    st->hook_calls++;                       /* 诊断：OS 到达了我们 */

    if (!st->armed) {
        st->drop_count++;                   /* 没有程序在监听：丢弃 */
        return;
    }
    type = *(unsigned *)(e + 4);
    if (type == 0x00100010u) {              /* key */
        unsigned sub = *(unsigned *)(e + 28);
        push_item(st, 1, sub == 16u ? 1 : 0,
                  *(unsigned short *)(e + 34), 0, 0, 0);
    } else if (type == 15u) {               /* touch */
        n = *(unsigned short *)(e + 24);
        if (n > 8)
            n = 8;
        for (i = 0; i < n; i++) {
            unsigned char *p = e + 28 + 12 * i;
            unsigned act = *(unsigned *)p;  /* 1=按下 2=移动 8=释放 */
            if (*(unsigned short *)(p + 4) != 0)
                continue;                   /* 无效项 */
            if (act != 1u && act != 8u)
                continue;   /* 移动会淹没队列；widget 只需“按下 + 释放”
                               即可登记一次点击 */
            push_item(st, 2, 0, 0,
                      *(unsigned short *)(p + 6),
                      *(unsigned short *)(p + 8), (unsigned char)act);
        }
    }
}
