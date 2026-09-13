/*
 * app_common.c —— app-collection 应用的公共运行支撑（toolchain 示例件）
 *
 * ⚠️ AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
 *
 * 实现取自 suika_prime.c 与 cube3d.c 中**逐字相同**的那部分样板。
 */
#include <stdint.h>
#include "app_common.h"

extern void *prime_sys_get_lcd(void);

/* Keep at least one runtime relocation for the existing ELF loader. */
static uint32_t *volatile relocation_anchor = (uint32_t *)&relocation_anchor;

int app_rd16(const uint8_t *p)
{
    return (int)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t app_rd32(const uint8_t *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

uint32_t *app_lcd_framebuffer(void)
{
    uint32_t *lcd = (uint32_t *)prime_sys_get_lcd();
    uint32_t **table;

    if (!lcd) return 0;
    table = *(uint32_t ***)lcd;
    return table ? *(uint32_t **)((uint8_t *)table + 0x10) : 0;
}

void app_blit_fb(uint32_t *dst, const uint32_t *src, int pixels)
{
    int i;

    for (i = 0; i < pixels; ++i) dst[i] = src[i];
}

void app_clear_fb(uint32_t *fb, int pixels, uint32_t color)
{
    int i;

    for (i = 0; i < pixels; ++i) fb[i] = color;
}

void app_put_px(uint32_t *fb, int fb_w, int fb_h, int x, int y, uint32_t color)
{
    if ((unsigned)x >= (unsigned)fb_w) return;
    if ((unsigned)y >= (unsigned)fb_h) return;
    fb[y * fb_w + x] = color;
}

/* Keep main away from address 0: the HP loader treats return 0 as failure.
 * 链接脚本 prime_dyn.ld 把 .text.entrypad 排在 .text 最前。 */
__attribute__((section(".text.entrypad"), used, noinline))
void app_elf_requirements(void)
{
    (void)relocation_anchor;
    __asm volatile("nop\n nop\n nop\n nop");
}
