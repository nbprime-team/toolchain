/*
 * app_common.h —— app-collection 应用的公共运行支撑（toolchain 示例件）
 *
 * ⚠️ AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
 *
 * suika 与 cube3d 原先各自复制了同一批样板（事件读取、LCD 帧缓冲、
 * 整屏拷贝、ELF 硬要求），现集中到此，两个应用共用一份。
 *
 * 用法（应用 Makefile）：
 *     include $(SDK)/templates/app.mk
 *     include $(SDK)/examples/app-common/app_common.mk
 */
#ifndef APP_COMMON_H
#define APP_COMMON_H

#include <stdint.h>

/* ---- 固件事件（与 PureDOOM / puredoom.elf 的 ui_event_prime_s 一致） ---- */
#define APP_EV_TICK     15u
#define APP_EV_KEY      0x00100010u
#define APP_KEY_DOWN    16u
#define APP_KEY_UP      0x00100000u
#define APP_TOUCH_BEGIN 1u
#define APP_TOUCH_MOVE  2u
#define APP_TOUCH_END   8u

/* ---- 事件缓冲的小端读取（按字节偏移访问） ---- */
int      app_rd16(const uint8_t *p);
uint32_t app_rd32(const uint8_t *p);

/* ---- 显示 ---- */
/* 固件 LCD 的帧缓冲基址（32bpp ARGB）；失败返回 NULL */
uint32_t *app_lcd_framebuffer(void);
/* 整屏拷贝：dst[i] = src[i]（pixels = 宽*高） */
void app_blit_fb(uint32_t *dst, const uint32_t *src, int pixels);
/* 填满帧缓冲 */
void app_clear_fb(uint32_t *fb, int pixels, uint32_t color);
/* 写像素（带裁剪） */
void app_put_px(uint32_t *fb, int fb_w, int fb_h, int x, int y, uint32_t color);

/* ---- ELF 硬要求（HP 加载器需要，两个应用都必须满足） ----
 * 1) 至少保留一个运行时重定位（R_ARM_RELATIVE）——否则 .rel.dyn 为空，
 *    退出时行为异常；
 * 2) main 不要落在地址 0 —— 本函数位于 .text.entrypad（链接脚本把它排到
 *    .text 最前），使 main 的地址非 0。
 * 在 main() 开头调用一次即可。 */
void app_elf_requirements(void);

#endif /* APP_COMMON_H */
