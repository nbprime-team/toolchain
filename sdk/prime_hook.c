/*
 * prime_hook.c —— 固件输入钩子（SDK）
 *
 * ⚠️ AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
 *
 * 机制与原 suika（app-collection/examples/suika）对 PureDOOM install_input_hack()
 * 的重建同构：把 0x307FBFA0 处 16 字节换成两字 trampoline
 * （ldr pc,[pc,#-4] + **用户回调地址**），借助 prime_input.S 的特权 memcpy
 * 完成写入与 cache 刷新。回调在固件上下文中直接执行，并自行调用
 * prime_sys_get_event() 取事件——与 suika 的 suika_event_hook 完全一致的路径。
 */
#include <stdint.h>
#include "prime_hook.h"

extern void prime_privileged_memcpy(void *dst, const void *src, uint32_t size);

/* 固件输入分发入口（取自 puredoom.elf；仅适用于同版本固件） */
#define PRIME_HOOK_TARGET 0x307FBFA0u

static uint8_t  saved_code[16] __attribute__((aligned(4)));
static uint32_t trampoline[2]  __attribute__((aligned(4)));
static prime_hook_fn g_callback;
static int g_installed;

int prime_hook_install(prime_hook_fn cb)
{
    if (g_installed || !cb) return 0;

    /* 1) 保存原始 16 字节 */
    prime_privileged_memcpy(saved_code, (const void *)PRIME_HOOK_TARGET, 16u);

    /* 2) 写 trampoline：ldr pc, [pc, #-4] ; .word <用户回调> */
    trampoline[0] = 0xE51FF004u;
    trampoline[1] = (uint32_t)(uintptr_t)cb;
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
