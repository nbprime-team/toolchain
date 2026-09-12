# toolchain/sdk —— Prime 用户程序 SDK（C 核心库）

> ⚠️ **AI 生成 / 辅助创作：DeepSeek V4.1 Flash**（未人工审查）

**C 核心库的唯一权威源**：`prime-tcc` 与 `app-collection` 的应用都**直接引用本目录**，
不再各自保留副本。

## 内容

| 文件 | 用途 |
|---|---|
| `prime.h` | 用户程序头文件（`stdbool.h` 包含、`printf` 变参宏、`size_t` 契约） |
| `stdbool.h` | C99 布尔 |
| `hp_gfx.h` / `hp_gfx.c` | 绘图：framebuffer、图元、文本、GROB |
| `hp_input.h` / `hp_input.c` | 键盘/触摸事件与输入钩子 |
| `hp_math.h` / `hp_math.c` | 数学（`hp_ldexp` 与旧版 `hp_double_to_str`；其余 `hp_*` 由 openlibm 提供） |
| `hp_string.h` / `hp_string.c` | 字符串与转换 |
| `hp_fonts.h` / `hp_gui.h` / `hp_sys.h` / `hp_fixmath.h` / `hp_random.h` / `hp_codec.h` / `hp_icall.h` / `hp_image.h` | 字体、控件、固件 SVC、Q16.16 定点、随机数、编解码、安全回调、PNG 图像（**实现缺失**，见 [../../prime-tcc/STATUS.md](../../prime-tcc/STATUS.md)） |
| `prime_dyn.ld` | 用户程序链接脚本（ELF32 PIE/DYN，`ENTRY(main)`） |
| `prime_input.S` | SVC 包装：`prime_sys_get_lcd` / `_sleep` / `_get_event` + 特权 memcpy |
| `prime_hook.c` / `.h` | **固件输入钩子**——取事件必须用它（见下） |
| `hook_abi.md` | 输入钩子 ABI 说明 |

## ⚠️ 取事件的正确方式

`prime_sys_get_event` 走 **SVC #0x1003f**，它**不能在普通循环里轮询**——只在固件的
事件分发路径中有效。直接轮询的表现是：程序能加载、入口被调用，但**随即卡死**。

正确做法（与 PureDOOM / suika 一致）：trampoline 直接指向你的回调，**回调内自行
调用 `prime_sys_get_event()` 取事件**。

```c
#include "prime_hook.h"

static void on_event(void *event) {
    prime_sys_get_event(event);       /* 取事件（SVC #0x1003f） */
    ...解析事件...
}

prime_hook_install(on_event);         /* 挂钩 0x307FBFA0 */
while (!quit) { render(); prime_sys_sleep(20); }
prime_hook_remove();                  /* 退出前还原固件原始代码 */
```

`suika` 与 `cube3d` 均按此方式使用（已无各自的内联实现）。

## 引用方式

```make
SDK ?= ../../../toolchain          # 从 app-collection/{examples,staging}/<app>/ 出发

CFLAGS  += -I$(SDK)/sdk
LDFLAGS += -Wl,-T,$(SDK)/sdk/prime_dyn.ld
OBJS    += prime_input.o           # 提供 prime_sys_*（必需）
OBJS    += prime_hook.o            # 需要输入时
```

`prime-tcc` 的 `deploy` 目标从本目录拷 `prime.h`、`stdbool.h` 与 `hp_*.h`
进部署目录。

## 维护约定

1. 改动即影响全部应用与 prime-tcc，需重新验证各自构建；
2. 契约与 ABI 约定见 [../../prime-tcc/API_ABI_CONTRACT.md](../../prime-tcc/API_ABI_CONTRACT.md)。
