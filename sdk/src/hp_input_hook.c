/* AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查） */
#include "hp_input_hook.h"

#define HP_EVENT_SLOT 0x307FBFA0u

extern void prime_privileged_memcpy(void *dst, const void *src, unsigned n);
extern int  prime_sys_get_event(void *ev);

static unsigned char g_saved[16] __attribute__((aligned(4)));
static unsigned g_tramp[2] __attribute__((aligned(4)));
static int g_on;

static volatile struct hp_event g_q[16];
static volatile unsigned g_head, g_tail;

static void push(unsigned type, unsigned code)
{
    unsigned next = (g_head + 1u) & 15u;
    if (next == g_tail) {
        return;                     /* 满则丢最旧由调用者处理 */
    }
    g_q[g_head].type = type;
    g_q[g_head].code = code;
    g_head = next;
}

static void hook(void *ev)
{
    unsigned char *p = (unsigned char *)ev;
    unsigned w7, w8;

    if (!prime_sys_get_event(ev)) {
        return;
    }
    w7 = *(unsigned *)(p + 28);
    w8 = *(unsigned *)(p + 32);
    if (w7 == 16u) {                /* 按下 */
        push(HP_EV_KEY, w8 >> 16);
    } else if (w7 == 1u) {          /* 触摸按下 */
        push(HP_EV_TOUCH, 0u);
    }
}

int hp_input_start(void)
{
    if (g_on) {
        return 1;
    }
    prime_privileged_memcpy(g_saved, (const void *)HP_EVENT_SLOT, 16u);
    g_tramp[0] = 0xE51FF004u;       /* ldr pc, [pc, #-4] */
    g_tramp[1] = (unsigned)(unsigned long)&hook;
    prime_privileged_memcpy((void *)HP_EVENT_SLOT, g_tramp, 8u);
    g_head = g_tail = 0;
    g_on = 1;
    return 1;
}

void hp_input_stop(void)
{
    if (!g_on) {
        return;
    }
    prime_privileged_memcpy((void *)HP_EVENT_SLOT, g_saved, 16u);
    g_on = 0;
}

int hp_input_poll(struct hp_event *out)
{
    if (g_tail == g_head) {
        return 0;
    }
    out->type = g_q[g_tail].type;
    out->code = g_q[g_tail].code;
    g_tail = (g_tail + 1u) & 15u;
    return 1;
}
