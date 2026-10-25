/*
 * keys.c —— 原生按键钩子示例（toolchain 接口示例）
 *
 * ⚠️ AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
 *
 * 目的：用**最小程序**验证「nbprime 原生按键处理」这条链路：
 *   1. 安装固件入口 0x307FBFA0 钩子（toolchain/sdk/src/prime_hook.c）；
 *   2. 回调里取事件（SVC 0x1003f），并把**原始字段**显示在屏幕上；
 *   3. 便于逐键核对 toolchain/sdk/docs/hook_abi.md 里的 ABI：
 *        +0x04  event_type   （按键事件 = 0x00100010）
 *        +0x1C  第一条记录   （= words[7]，16=按下 / 0x100000=抬起）
 *        +0x22  key_code0    （= words[8]>>16，物理扫描码）
 *   4. ESC / ON 退出。
 *
 * 显示内容每帧刷新：
 *     ty4  = *(u32*)(ev+4)          —— 事件大类
 *     w7   = *(u32*)(ev+28)         —— 按键 按下/抬起
 *     w8   = *(u32*)(ev+32)         —— 高 16 位为扫描码
 *     KEY  = w8 >> 16               —— 物理扫描码（与 hp_input.h 的 HP_KEY_* 对比）
 *     n    = 累计收到的事件数
 *     LAST = 最近 8 个扫描码
 */

#include <stdint.h>
#include "prime_hook.h"      /* 原生钩子（SDK） */
#include "app_common.h"      /* LCD / 整屏拷贝 / 事件读取 / ELF 硬要求 */

#define LCD_W 320
#define LCD_H 240

#define C_BG   0xff101418u
#define C_FG   0xffe8f0f8u
#define C_KEY  0xffffc857u
#define C_HINT 0xff8a9bb0u

extern void prime_sys_sleep(uint32_t ms);

/* ---- 内置 8x8 十六进制字模（MSB 在左），避免依赖字体资源 ---- */
static const uint8_t g_hex8[16][8] = {
    { 0x3c,0x66,0x66,0x66,0x66,0x66,0x3c,0x00 }, /* 0 */
    { 0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00 }, /* 1 */
    { 0x3c,0x66,0x06,0x0c,0x30,0x60,0x7e,0x00 }, /* 2 */
    { 0x3c,0x66,0x06,0x1c,0x06,0x66,0x3c,0x00 }, /* 3 */
    { 0x0c,0x1c,0x3c,0x6c,0x7e,0x0c,0x0c,0x00 }, /* 4 */
    { 0x7e,0x60,0x7c,0x06,0x06,0x66,0x3c,0x00 }, /* 5 */
    { 0x1c,0x30,0x60,0x7c,0x66,0x66,0x3c,0x00 }, /* 6 */
    { 0x7e,0x66,0x0c,0x18,0x18,0x18,0x18,0x00 }, /* 7 */
    { 0x3c,0x66,0x66,0x3c,0x66,0x66,0x3c,0x00 }, /* 8 */
    { 0x3c,0x66,0x66,0x3e,0x06,0x0c,0x38,0x00 }, /* 9 */
    { 0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0x00 }, /* A */
    { 0x7c,0x66,0x66,0x7c,0x66,0x66,0x7c,0x00 }, /* b */
    { 0x3c,0x66,0x60,0x60,0x60,0x66,0x3c,0x00 }, /* C */
    { 0x78,0x6c,0x66,0x66,0x66,0x6c,0x78,0x00 }, /* d */
    { 0x7e,0x60,0x60,0x7c,0x60,0x60,0x7e,0x00 }, /* E */
    { 0x7e,0x60,0x60,0x7c,0x60,0x60,0x60,0x00 }, /* F */
};

/* 画一个十六进制数字（scale 倍放大） */
static void draw_nib(uint32_t *fb, int x, int y, int scale, unsigned v,
                     uint32_t col)
{
    const uint8_t *g = g_hex8[v & 15u];
    int r, c, sy, sx;

    for (r = 0; r < 8; ++r) {
        for (c = 0; c < 8; ++c) {
            if (!((g[r] >> (7 - c)) & 1u)) continue;
            for (sy = 0; sy < scale; ++sy)
                for (sx = 0; sx < scale; ++sx)
                    app_put_px(fb, LCD_W, LCD_H,
                               x + c * scale + sx, y + r * scale + sy, col);
        }
    }
}

/* 画一串十六进制数字（如 0x00510000 → "00510000"） */
static void draw_hex32(uint32_t *fb, int x, int y, int scale, unsigned v,
                       uint32_t col)
{
    int i;

    for (i = 7; i >= 0; --i) {
        draw_nib(fb, x + (7 - i) * 9 * scale, y, scale, (v >> (i * 4)) & 15u, col);
    }
}

/* ---- 事件回调（固件上下文）：只记状态，绘制留给主循环 ---- */
static volatile unsigned g_ty4, g_w7, g_w8;   /* 原始字段 */
static volatile int      g_key = -1;          /* 扫描码 */
static volatile unsigned g_n;                 /* 事件计数 */
static volatile int      g_last[8];           /* 最近 8 个扫描码 */
static volatile int      g_quit;

static void on_event(void *event)
{
    uint8_t *p = (uint8_t *)event;

    prime_sys_get_event(event);               /* SDK 契约：回调内自行取事件 */

    /* 判据以 words[7] 为准（实测：按键为 16 按下 / 0x100000 抬起）。
     * +0x04 随按下/抬起变化（0x00100010 / 0x00000020），只作显示。 */
    {
        unsigned w7 = app_rd32(p + 28);
        unsigned w8 = app_rd32(p + 32);

        if (w7 != 16u && w7 != 0x100000u) return;

        {
        int scan = (int)(w8 >> 16);

        g_ty4 = app_rd32(p + 4);
        g_w7 = w7;
        g_w8 = w8;
        g_key = scan;
        if (w7 == 16u) {                      /* 按下 */
            int i;
            for (i = 7; i > 0; --i) g_last[i] = g_last[i - 1];
            g_last[0] = scan;
            if (scan == 0x01 /* ESC */ || scan == 0x83 /* ON */) g_quit = 1;
        }
        if (g_n < 0xFFFFFFFFu) g_n++;
        }
    }
}

static uint32_t framebuf[LCD_W * LCD_H] __attribute__((aligned(32)));

__attribute__((section(".text.main"), noinline))
int main(void *config, void *reserved)
{
    uint32_t *lcd;
    int i;

    (void)config;
    (void)reserved;
    app_elf_requirements();
    lcd = app_lcd_framebuffer();
    if (!lcd) return 0;
    if (!prime_hook_install(on_event)) return 1;

    while (!g_quit) {
        app_clear_fb(framebuf, LCD_W * LCD_H, C_BG);

        /* 原始字段（便于对照 hook_abi.md） */
        draw_hex32(framebuf, 8, 16, 2, g_ty4, C_FG);
        draw_hex32(framebuf, 8, 48, 2, g_w7, C_FG);
        draw_hex32(framebuf, 8, 80, 2, g_w8, C_FG);

        /* 扫描码（大字） */
        if (g_key >= 0) {
            draw_nib(framebuf, 8, 120, 6, (unsigned)g_key >> 4, C_KEY);
            draw_nib(framebuf, 8 + 9 * 6, 120, 6, (unsigned)g_key & 15u, C_KEY);
        }

        /* 事件计数 */
        draw_hex32(framebuf, 8, 184, 1, g_n, C_HINT);

        /* 最近 8 个扫描码 */
        for (i = 0; i < 8; ++i) {
            if (g_last[i] < 0) break;
            draw_nib(framebuf, 200 + i * 14, 200, 1, (unsigned)g_last[i] >> 4, C_HINT);
            draw_nib(framebuf, 200 + i * 14 + 7, 200, 1, (unsigned)g_last[i] & 15u, C_HINT);
        }

        app_blit_fb(lcd, framebuf, LCD_W * LCD_H);
        prime_sys_sleep(30);
    }

    prime_hook_remove();
    return 0;
}
