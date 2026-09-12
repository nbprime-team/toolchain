# toolchain/sdk —— Prime 用户程序 SDK（C 核心库）

> ⚠️ **AI 生成 / 辅助创作：DeepSeek V4.1 Flash**（未人工审查）

**C 核心库的唯一权威源**：`prime-tcc` 与 `app-collection` 的应用都**直接引用本目录**，
不再各自保留副本。

| 文件 | 用途 |
|---|---|
| `prime.h` | 用户程序头文件（`stdbool.h` 包含、`printf` 变参宏、`size_t` 契约） |
| `stdbool.h` | C99 布尔 |
| `prime_dyn.ld` | 用户程序链接脚本（ELF32 PIE/DYN，`ENTRY(main)`） |
| `prime_input.S` | SVC 包装：`prime_sys_get_lcd` / `_sleep` / `_get_event` + 特权 memcpy |
| `prime_hook.c` / `.h` | **固件输入钩子**——取事件必须用它（见下） |
| `hook_abi.md` | 输入钩子 ABI 说明 |

## ⚠️ 取事件的正确方式

`prime_sys_get_event` 走 **SVC #0x1003f**，它**不能在普通循环里轮询**——只在固件的
事件分发路径中有效。直接轮询的表现是：程序能加载、入口被调用，但**随即卡死**。

正确做法（与 PureDOOM / 原 suika 一致）：

```c
#include "prime_hook.h"

static void on_event(void *event) {   /* 固件回调；在此解析事件 */ }

prime_hook_install(on_event);         /* 挂钩 0x307FBFA0 */
while (!quit) { render(); prime_sys_sleep(20); }
prime_hook_remove();                  /* 退出前还原固件原始代码 */
```

## 引用方式

```make
SDK ?= ../../../toolchain          # 从 app-collection/{examples,staging}/<app>/ 出发

CFLAGS  += -I$(SDK)/sdk
LDFLAGS += -Wl,-T,$(SDK)/sdk/prime_dyn.ld
OBJS    += prime_input.o           # 提供 prime_sys_*（必需）
OBJS    += prime_hook.o            # 需要输入时
```

`prime-tcc` 的 `deploy` 目标从本目录拷 `prime.h`/`stdbool.h` 进部署目录。

## 为什么放在 toolchain 而不是 prime-tcc

toolchain 是**基础设施/构建环境**层，SDK 是**所有 Prime 用户程序**的公共构建件；
prime-tcc 只是使用者之一（它编译 TCC 本体、部署运行时）。放在 toolchain 可以避免
"公共库跟着某个具体仓库走"。

## 维护约定

1. 改动即影响全部应用与 prime-tcc，需重新验证各自构建；
2. 历史差异：`prime_input.S` 曾在两个应用中各存一份（只差一行注释），现以本目录为准；
3. 契约与 ABI 约定见 [../../prime-tcc/API_ABI_CONTRACT.md](../../prime-tcc/API_ABI_CONTRACT.md)。
