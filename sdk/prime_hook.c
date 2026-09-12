/*
 * prime_hook.c —— 固件输入钩子（SDK）
 *
 * ⚠️ AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
 *
 * 机制提取自 suika（app-collection/examples/suika/suika_prime.c）中对
 * PureDOOM install_input_hack() 的重建：把 0x307FBFA0 处 16 字节换成
 * 两字 trampoline（ldr pc,[pc,#-4] + 回调地址），并借助 prime_input.S 的
 * 特权 memcpy 完成写入与 cache 刷新。
 */
#include <stdint.h>
#include "prime_hook.h"

extern void prime_privileged_memcpy(void *dst, const void *src, uint32_t size);
extern int  prime_sys_get_event(void *event);

/* 固件输入分发入口（取自 puredoom.elf；仅适用于同版本固件） */
#define PRIME_HOOK_TARGET 0x307FBFA0u

static uint8_t  saved_code[16] __attribute__((aligned(4)));
static uint32_t trampoline[2]  __attribute__((aligned(4)));
static prime_hook_fn g_callback;
static int g_installed;

/*
 * 固件跳转目标。必须 noinline+used：地址会被写进 trampoline。
 * 本文件由 GCC 编译（非 TCC），callee-saved 寄存器由编译器负责保存。
 */
__attribute__((noinline, used))
static void prime_hook_entry(void *event)
{
    if (!event || !g_callback) return;

    /* 填充事件缓冲。注意：**不要**用返回值决定是否回调——suika 的可用实现
     * 也是在调用后直接解析 event（该 SVC 的返回值不可靠，用它判断会导致
     * 回调被全部跳过，表现为按键/触摸无响应）。 */
    prime_sys_get_event(event);
    g_callback(event);
}

int prime_hook_install(prime_hook_fn cb)
{
    if (g_installed || !cb) return 0;

    /* 1) 保存原始 16 字节 */
    prime_privileged_memcpy(saved_code, (const void *)PRIME_HOOK_TARGET, 16u);

    /* 2) 写 trampoline：ldr pc, [pc, #-4] ; .word prime_hook_entry */
    trampoline[0] = 0xE51FF004u;
    trampoline[1] = (uint32_t)(uintptr_t)&prime_hook_entry;
    prime_privileged_memcpy((void *)PRIME_HOOK_TARGET, trampoline, 8u);

    g_callback = cb;
    g_installed = 1;
    return 1;
}

void prime_hook_remove(void)
{
    if (!g_installed) return;

    prime_privileged_memcpy((void *)PRIME_HOOK_TARGET, saved_code, 16u);
    g_callback = 0;
    g_installed = 0;
}

int prime_hook_active(void)
{
    return g_installed;
}
