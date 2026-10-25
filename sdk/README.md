# toolchain/sdk —— Prime 用户程序 SDK（C 核心库）

> ⚠️ **AI 生成 / 辅助创作：DeepSeek V4.1 Flash**（未人工审查）

Prime 用户程序（TCC 编译或交叉 gcc 编译）的 **C 核心库**，也是工作区内的**唯一权威源**：
`prime-tcc`、`app-collection` 都直接引用本目录，**不再各自保留副本**。

## 1. 分层

按**抽象层次**分三层 + 工具/文档，越往下越贴近固件：

| 层 | 目录 | 内容 | 职责 |
|---|---|---|---|
| **L0 契约层** | `include/` | `prime.h`、`stdbool.h`、`prime_hook.h`、`hp_*.h` | 用户程序可见 API、ABI 约定、`size_t` 契约 |
| **L0′ TCC 专用头** | `include/tcc/` | `hp_libc.h`、`hp_input_svc.h`、`stdio.h`、`semaphore.h`、`sys/*.h` | **仅供 TCC 自身编译**（`stdio.h` 刻意遮蔽 newlib，使 `FILE` 指向 `HP_FILE`）；**不要加入应用 `-I`** |
| **L1 实现层** | `src/` | 用户运行时 `hp_*.c`（输出/内存/字符串/数学/绘图/输入/字体/控件/图像/定点/随机/编解码/间接调用/固件封装）+ TCC 自身运行时 `hp_libc.c`、`hp_vsnprintf.c` + `prime_hook.c`、`rt_softfloat.c` | 纯 C 实现（不含固件汇编） |
| **L1 数学库** | `src/openlibm/` | openlibm（BSD）源码；编译时经 `-D` 重命名为 `hp_sin`/`hp_cos`/… 以对接 `hp_math.h` | 超越函数 |
| **L2 平台层** | `platform/` | `prime_input.S`、`hp_svc.s`、`rt_svc.s`（SVC 包装/crt0）、`prime_dyn.ld`（链接脚本） | 固件直连（`svc`、PIE/PT_LOAD 布局） |
| **工具** | `tools/` | `fontgen2.py`、`fontcompare.py`（生成/比对 `hp_fonts_data.c`） | 字模数据生成 |
| **文档** | `docs/` | `hook_abi.md` | 输入钩子 ABI 说明 |

引用方式（应用 Makefile）：

```make
CFLAGS  += -I$(SDK)/sdk/include -I$(SDK)/sdk/include/sys
LDFLAGS += -Wl,-T,$(SDK)/sdk/platform/prime_dyn.ld
```

`toolchain/templates/app.mk` 已封装上述路径与 `platform/prime_input.S`、`src/prime_hook.c`
的编译规则；`toolchain/examples/api-probe/` 是只用 `include/` 的两端编译探针。

## 2. 模块清单（抽象层次 → 状态）

| 模块 | 头（L0） | 实现（L1/L2） | 状态 |
|---|---|---|---|
| 基础运行时（输出/内存/字符串） | `prime.h` | `hp_rt.c` | ✅ 完整 |
| C 字符串扩展 | `hp_string.h` | `hp_string.c` | ✅ 完整 |
| 数学 | `hp_math.h` | `hp_math.c` + `src/openlibm/` | ✅ 完整 |
| 绘图 | `hp_gfx.h` | `hp_gfx.c` | ✅ 完整 |
| 输入 | `hp_input.h` | `hp_input.c` + `hp_input_svc.c` | ✅ 完整 |
| 输入钩子 | `prime_hook.h` | `prime_hook.c` | ✅ 完整 |
| 字体 | `hp_fonts.h` | `hp_fonts.c` + `hp_fonts_data.c` | ✅ 完整 |
| 控件（GUI） | `hp_gui.h` | `hp_gui.c` | ✅ 完整 |
| 图像（PNG） | `hp_image.h` | `hp_image.c` | ✅ 完整 |
| Q16.16 定点 | `hp_fixmath.h` | `hp_fixmath.c` | ✅ 完整 |
| 随机/噪声 | `hp_random.h` | `hp_random.c` | ✅ 完整 |
| 校验/编解码 | `hp_codec.h` | `hp_codec.c` | ✅ 完整 |
| 安全间接调用 | `hp_icall.h` | `hp_icall.c` | ✅ 完整 |
| 固件 SVC 封装 | `hp_sys.h` | `hp_sys.c` | ✅ 完整 |
| TCC 自身运行时 | `hp_libc.h`、`stdio.h`、`sys/*.h` | `hp_libc.c`、`hp_vsnprintf.c`、`platform/hp_svc.s` | ✅ 完整 |
| 软浮点/AEABI 辅助 | — | `rt_softfloat.c`（+ 由 `prime-tcc` 打包 libgcc 的 `_aeabi_*`） | ✅ 完整 |

## 3. 统一 ABI 约定

**`size_t` 单一契约**：`prime.h`、`hp_string.h`、`hp_sys.h`、`stdio.h` 共用同一组
guard，避免多个头同时包含时重复定义：

```c
#ifndef PRIME_SIZE_T_DEFINED
#define PRIME_SIZE_T_DEFINED
#define SIZE_T_DEFINED
#if defined(__SIZE_TYPE__)
typedef __SIZE_TYPE__ size_t;
#else
typedef unsigned int size_t;
#endif
#endif
```

新增任何定义 `size_t` 的头，都必须使用同一 guard。

## 4. 依赖的外部符号（须由 L2 或应用提供）

| 符号 | 提供者 |
|---|---|
| `prime_sys_get_lcd` / `prime_sys_sleep` / `prime_sys_get_event` | `platform/prime_input.S` |
| `prime_privileged_memcpy` | `platform/prime_input.S` |
| `hp_svc_*`（固件堆、fopen 等） | `platform/rt_svc.s` |
| `prime_hook_install` / `prime_hook_remove` / `prime_hook_active` | `src/prime_hook.c` |

## 5. 取事件的正确方式（原生路线）

`prime_sys_get_event` 走 **SVC #0x1003f**，**不能在普通循环里轮询**——原生固件下它只在
**事件分发路径**中有效（直接轮询会卡死）。原生路线的做法与 PureDOOM / suika 一致：
用 `prime_hook_install()` 把固件入口 `0x307FBFA0` 换成指向你回调的 trampoline，
**回调内自行调用 `prime_sys_get_event()`**。

```c
#include "prime_hook.h"

static void on_event(void *event) {
    prime_sys_get_event(event);       /* 取事件（SVC #0x1003f） */
    ...解析事件...
}

prime_hook_install(on_event);
while (!quit) { render(); prime_sys_sleep(20); }
prime_hook_remove();                  /* 退出前还原固件原始代码 */
```

细节见 [`docs/hook_abi.md`](docs/hook_abi.md)。`app-collection` 的 `suika`、`cube3d`
以及 `app-collection/staging/prime-markdown` 均按此方式使用。

## 6. 复核命令

```bash
# 逐头核对"声明但无实现"（应为空）
cd toolchain/sdk
python3 - <<'EOF'
import re, glob, os
def syms(t): return set(re.findall(r'\b(hp_[a-z0-9_]+)\s*\(', t))
src = ''.join(open(c, encoding='utf-8').read() for c in glob.glob('src/*.c'))
defined = syms(src)
for h in sorted(glob.glob('include/hp_*.h')):
    miss = sorted(syms(open(h, encoding='utf-8').read()) - defined)
    if miss: print(os.path.basename(h), len(miss), ','.join(miss))
EOF
# 说明：hp_math.h 的超越函数由 src/openlibm/ 在编译期重命名后提供（见 prime-tcc/Makefile 的 OLM_RENAMES）
```

## 7. 维护约定

1. 本目录是 C 核心库的**唯一权威源**，改动会影响 `prime-tcc` 与 `app-collection` 全部工程；
   `prime-tcc` **不保留副本**，其 `Makefile` 直接引用本目录；构建 `.hpappdir` 时由
   `make deploy` 把头文件与产物拷入部署目录。
2. 补实现时**以头文件契约为准**，并同步更新本文件。
3. 契约与 ABI 约定见 [`../../prime-tcc/API_ABI_CONTRACT.md`](../../prime-tcc/API_ABI_CONTRACT.md)。
4. 分层调整（新增/移动文件）后，同步 `toolchain/templates/app.mk`、
   `toolchain/examples/api-probe/Makefile` 与 `prime-tcc/Makefile` 的路径。
