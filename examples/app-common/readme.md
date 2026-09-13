# app-common —— 应用公共运行支撑（示例件）

> ⚠️ **AI 生成 / 辅助创作：DeepSeek V4.1 Flash**（未人工审查）

`app-collection` 的 `suika` 与 `cube3d` 原先各自复制了**同一批样板**——事件缓冲的
小端读取、LCD 帧缓冲定位、整屏拷贝、以及两项 ELF 硬要求。本目录把它们集中一份，
两个应用共用（与 `prime_hook.c`、`resources/prime-unifont` 一起构成"统一架构"）。

## 文件

| 文件 | 内容 |
|---|---|
| `app_common.h` | 事件常量（`APP_EV_*`/`APP_KEY_*`/`APP_TOUCH_*`）与接口声明 |
| `app_common.c` | 实现：`app_rd16/32`、`app_lcd_framebuffer`、`app_blit_fb`、`app_clear_fb`、`app_put_px`、`app_elf_requirements` |
| `app_common.mk` | 公共构建片段（包含路径与编译规则，并并入 `OBJS`） |

## 用法

```make
include $(SDK)/templates/app.mk
include $(SDK)/examples/app-common/app_common.mk
```

```c
#include "app_common.h"

int main(void *config, void *reserved)
{
    uint32_t *lcd;

    app_elf_requirements();             /* ELF 硬要求：重定位 + main 非 0 */
    lcd = app_lcd_framebuffer();
    ...
    app_clear_fb(fb, W * H, color);
    app_blit_fb(lcd, fb, W * H);
}
```

## 约定

- **ELF 硬要求**：`app_elf_requirements()` 位于 `.text.entrypad`（链接脚本
  `prime_dyn.ld` 把它排到 `.text` 最前），并保留一个 `R_ARM_RELATIVE`；`main` 须加
  `__attribute__((section(".text.main")))`，紧随其后；
- 这里的函数都是**固件直连**的最小实现，待 `toolchain/sdk` 的 `hp_*` 实现补齐后，
  可整体替换为 `hp_gfx` 等同名能力。
