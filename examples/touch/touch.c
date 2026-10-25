/*
 * touch.c —— 原生触摸钩子示例（toolchain 接口示例）
 *
 * ⚠️ AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
 *
 * 与 keys.c 同一套原生钩子（0x307FBFA0 + SVC 0x1003f），本示例显示**触摸**字段：
 *     ty4  = *(u32*)(ev+4)      —— 触摸事件大类（= 15）
 *     w7   = *(u32*)(ev+28)     —— 动作（1=按下 2=移动 8=抬起）
 *     X    = *(u32*)(ev+32)>>16
 *     Y    = *(u32*)(ev+36)
 * 屏幕上同时画一个十字标记最近触点，便于核对坐标是否与实际手指一致。
 *
 * ESC / ON 退出。
 */

#include <stdint.h>
#include "prime_hook.h"
#include "app_common.h"

#define LCD_W 320
#define LCD_H 240

#define C_BG   0xff101418u
#define C_FG   0xffe8f0f8u
#define C_DOT  0xffff5f6du
#define C_HINT 0xff8a9bb0u

extern void prime_sys_sleep(uint32_t ms);

static const uint8_t g_hex8[16][8] = {
    { 0x3c,0x66,0x66,0x66,0x66,0x66,0x3c,0x00 },
    { 0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00 },
    { 0x3c,0x66,0x06,0x0c,0x30,0x60,0x7e,0x00 },
    { 0x3c,0x66,0x06,0x1c,0x06,0x66,0x3c,0x00 },
    { 0x0c,0x1c,0x3c,0x6c,0x7e,0x0c,0x0c,0x00 },
    { 0x7e,0x60,0x7c,0x06,0x06,0x66,0x3c,0x00 },
    { 0x1c,0x30,0x60,0x7c,0x66,0x66,0x3c,0x00 },
    { 0x7e,0x66,0x0c,0x18,0x18,0x18,0x18,0x00 },
    { 0x3c,0x66,0x66,0x3c,0x66,0x66,0x3c,0x00 },
    { 0x3c,0x66,0x66,0x3e,0x06,0x0c,0x38,0x00 },
    { 0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0x00 },
    { 0x7c,0x66,0x66,0x7c,0x66,0x66,0x7c,0x00 },
    { 0x3c,0x66,0x60,0x60,0x60,0x66,0x3c,0x00 },
    { 0x78,0x6c,0x66,0x66,0x66,0x6c,0x78,0x00 },
    { 0x7e,0x60,0x60,0x7c,0x60,0x60,0x7e,0x00 },
    { 0x7e,0x60,0x60,0x7c,0x60,0x60,0x60,0x00 },
};

static void draw_nib(uint32_t *fb, int x, int y, int scale, unsigned v,
                     uint32_t col)
{
    const uint8_t *g = g_hex8[v & 15u];
    int r, c, sy, sx;

    for (r = 0; r < 8; ++r)
        for (c = 0; c < 8; ++c) {
            if (!((g[r] >> (7 - c)) & 1u)) continue;
            for (sy = 0; sy < scale; ++sy)
                for (sx = 0; sx < scale; ++sx)
                    app_put_px(fb, LCD_W, LCD_H,
                               x + c * scale + sx, y + r * scale + sy, col);
        }
}

static void draw_hex32(uint32_t *fb, int x, int y, int scale, unsigned v,
                       uint32_t col)
{
    int i;
    for (i = 7; i >= 0; --i)
        draw_nib(fb, x + (7 - i) * 9 * scale, y, scale, (v >> (i * 4)) & 15u, col);
}

static void draw_cross(uint32_t *fb, int x, int y, uint32_t col)
{
    int i;
    for (i = -6; i <= 6; ++i) {
        app_put_px(fb, LCD_W, LCD_H, x + i, y, col);
        app_put_px(fb, LCD_W, LCD_H, x, y + i, col);
    }
}

/* ---- 事件回调 ---- */
static volatile unsigned g_ty4, g_w7, g_w8, g_w9;
static volatile int      g_x, g_y, g_down;
static volatile unsigned g_n;
static volatile int      g_quit;

static void on_event(void *event)
{
    uint8_t *p = (uint8_t *)event;

    prime_sys_get_event(event);

    /* 判据以 words[7] 为准：触摸为 1/2/8，按键为 16/0x100000（实测见 hook_abi.md） */
    {
        unsigned w7 = app_rd32(p + 28);
        unsigned w8 = app_rd32(p + 32);
        unsigned w9 = app_rd32(p + 36);

        if (w7 != 1u && w7 != 2u && w7 != 8u) {
            /* 非触摸：顺带支持按键退出 */
            if ((w7 == 16u) &&
                (((int)(w8 >> 16) == 0x01) || ((int)(w8 >> 16) == 0x83))) g_quit = 1;
            return;
        }

        g_ty4 = app_rd32(p + 4);
        g_w7 = w7;
        g_w8 = w8;
        g_w9 = w9;
        g_x = (int)(w8 >> 16);
        g_y = (int)w9;
        g_down = (w7 == 1u || w7 == 2u) ? 1 : 0;
        if (g_n < 0xFFFFFFFFu) g_n++;
    }
}

static uint32_t framebuf[LCD_W * LCD_H] __attribute__((aligned(32)));

__attribute__((section(".text.main"), noinline))
int main(void *config, void *reserved)
{
    uint32_t *lcd;

    (void)config;
    (void)reserved;
    app_elf_requirements();
    lcd = app_lcd_framebuffer();
    if (!lcd) return 0;
    if (!prime_hook_install(on_event)) return 1;

    while (!g_quit) {
        app_clear_fb(framebuf, LCD_W * LCD_H, C_BG);

        draw_hex32(framebuf, 8, 8, 1, g_ty4, C_FG);   /* 事件大类 */
        draw_hex32(framebuf, 8, 24, 1, g_w7, C_FG);   /* 动作 1/2/8 */
        draw_hex32(framebuf, 8, 40, 1, g_w8, C_FG);   /* x 在高 16 位 */
        draw_hex32(framebuf, 8, 56, 1, g_w9, C_FG);   /* y */
        draw_hex32(framebuf, 8, 72, 1, g_n, C_HINT);  /* 计数 */

        /* 大字坐标 */
        draw_nib(framebuf, 8, 100, 4, (unsigned)g_x >> 12, C_DOT);
        draw_nib(framebuf, 44, 100, 4, ((unsigned)g_x >> 8) & 15u, C_DOT);
        draw_nib(framebuf, 80, 100, 4, ((unsigned)g_x >> 4) & 15u, C_DOT);
        draw_nib(framebuf, 116, 100, 4, (unsigned)g_x & 15u, C_DOT);
        draw_nib(framebuf, 170, 100, 4, (unsigned)g_y >> 12, C_DOT);
        draw_nib(framebuf, 206, 100, 4, ((unsigned)g_y >> 8) & 15u, C_DOT);
        draw_nib(framebuf, 242, 100, 4, ((unsigned)g_y >> 4) & 15u, C_DOT);
        draw_nib(framebuf, 278, 100, 4, (unsigned)g_y & 15u, C_DOT);

        /* 触点标记（仅按住时画） */
        if (g_down && g_x > 0 && g_x < LCD_W && g_y > 130 && g_y < LCD_H)
            draw_cross(framebuf, g_x, g_y, C_DOT);

        /* 底部提示区（示意：按住并拖动看坐标变化） */
        draw_hex32(framebuf, 8, 200, 1, g_down, C_HINT);

        app_blit_fb(lcd, framebuf, LCD_W * LCD_H);
        prime_sys_sleep(30);
    }

    prime_hook_remove();
    return 0;
}
