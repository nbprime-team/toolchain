# toolchain/sdk —— Prime 用户程序 SDK（C 核心库）

> ⚠️ **AI 生成 / 辅助创作：DeepSeek V4.1 Flash**（未人工审查）

Prime 用户程序（TCC 编译或交叉 gcc 编译）的 **C 核心库**，也是工作区内的**唯一权威源**：
`prime-tcc`、`app-collection` 都直接引用本目录，不再各自保留副本。

## 1. 分层

按**抽象层次**分三层 + 文档，越往下越贴近固件：

| 层 | 目录 | 内容 | 职责 |
|---|---|---|---|
| **L0 契约层** | `include/` | `prime.h`、`stdbool.h`、`prime_hook.h`、`hp_*.h` | 用户可见 API、ABI 约定、`size_t` 契约 |
| **L1 实现层** | `src/` | `hp_rt.c`、`hp_string.c`、`hp_math.c`、`hp_gfx.c`、`hp_input.c`、`prime_hook.c` | 纯 C 实现（不含固件汇编） |
| **L2 平台层** | `platform/` | `prime_input.S`（SVC 包装）、`prime_dyn.ld`（链接脚本） | 固件直连（`svc`、PIE/PT_LOAD 布局） |
| **文档** | `docs/` | `hook_abi.md` | 输入钩子 ABI 说明 |

引用方式（应用 Makefile）：

```make
CFLAGS  += -I$(SDK)/sdk/include
LDFLAGS += -Wl,-T,$(SDK)/sdk/platform/prime_dyn.ld
```

`toolchain/templates/app.mk` 已封装上述路径与 `platform/prime_input.S`、`src/prime_hook.c`
的编译规则；`toolchain/examples/api-probe/` 是只用 `include/` 的两端编译探针。

## 2. 模块清单（抽象层次 → 状态）

| 模块 | 头（L0） | 实现（L1） | 状态 |
|---|---|---|---|
| 基础运行时（输出/内存/字符串） | `prime.h` | `hp_rt.c` | ⚠️ 部分 |
| C 字符串扩展 | `hp_string.h` | `hp_string.c` | ⚠️ 部分 |
| 数学 | `hp_math.h` | `hp_math.c` + **openlibm** | ⚠️ openlibm 源码缺失 |
| 绘图 | `hp_gfx.h` | `hp_gfx.c` | ⚠️ 部分 |
| 输入 | `hp_input.h` | `hp_input.c` | ⚠️ 部分 |
| 输入钩子 | `prime_hook.h` | `prime_hook.c` | ✅ 完整 |
| 字体 | `hp_fonts.h` | — | ❌ 无实现 |
| 控件（GUI） | `hp_gui.h` | — | ❌ 无实现 |
| 图像（PNG） | `hp_image.h` | — | ❌ 无实现 |
| Q16.16 定点 | `hp_fixmath.h` | — | ❌ 无实现 |
| 随机/噪声 | `hp_random.h` | — | ❌ 无实现 |
| 校验/编解码 | `hp_codec.h` | — | ❌ 无实现 |
| 安全间接调用 | `hp_icall.h` | — | ❌ 无实现 |
| 固件 SVC 封装 | `hp_sys.h` | — | ❌ 无实现 |

## 3. 缺漏清单（详实）

> 统计方法：提取各头中的函数声明，与 `src/*.c` 中的定义取差集（去重）。
> 复核命令：见 §5。

### 3.1 整模块缺实现（头在，`.c` 不在）

| 头 | 应放位置 | 缺失函数 | 数量 |
|---|---|---|---|
| `hp_codec.h` | `src/hp_codec.c` | `hp_crc32` `hp_adler32` `hp_hex_encode` `hp_hex_decode` `hp_b64_encode` `hp_b64_decode` | 6 |
| `hp_fixmath.h` | `src/hp_fixmath.c` | `hp_fx_int` `hp_fx_trunc` `hp_fx_round` `hp_fx_to_double` `hp_fx_from_double` `hp_fx_neg` `hp_fx_add` `hp_fx_sub` `hp_fx_mul` `hp_fx_div` `hp_fx_abs` `hp_fx_lerp` `hp_fx_sqrt` `hp_fx_sin` `hp_fx_cos` `hp_fx_tan` `hp_fx_atan2` | 17 |
| `hp_fonts.h` | `src/hp_fonts.c` + `src/hp_fonts_data.c` | `hp_font_mono` `hp_font_prop` `hp_font_mono_data` `hp_font_prop_data` `hp_font_h` `hp_font_w` `hp_font_advance` `hp_font_draw` `hp_font_draw_bg` | 9 |
| `hp_gui.h` | `src/hp_gui.c` | `hp_gui_begin` `hp_gui_end` `hp_gui_draw` `hp_gui_handle` `hp_gui_edit_insert` `hp_gui_key_char` | 6 |
| `hp_icall.h` | `src/hp_icall.c` | `hp_icall2` | 1 |
| `hp_image.h` | `src/hp_image.c` | `hp_image_load_png` `hp_image_load_png_mem` `hp_image_free` `hp_image_w` `hp_image_h` `hp_image_draw` `hp_image_draw_key` `hp_image_draw_scaled`（另 `hp_blit` 见 §3.4） | 8 |
| `hp_random.h` | `src/hp_random.c` | `hp_rng_seed` `hp_rng_u32` `hp_rng_range` `hp_rng_fx` `hp_noise2_hash` `hp_noise2_fx`（另 `hp_fx_int` 属定点模块） | 6 |
| `hp_sys.h` | `src/hp_sys.c` | `hp_sys_malloc` `hp_sys_calloc` `hp_sys_realloc` `hp_sys_free` `hp_sys_max_alloc` `hp_sys_heap_free` `hp_sys_sleep` `hp_sys_get_lcd` `hp_sys_get_event` `hp_sys_thread_create` `hp_sys_debug_open` `hp_sys_fopen` `hp_sys_fclose` `hp_sys_fread` `hp_sys_fwrite` `hp_sys_fseek` `hp_sys_ftell` `hp_sys_filesize` | 18 |
| `hp_math.h` | `src/openlibm/`（27 个 `.c`，第三方） | `hp_sqrt` `hp_exp` `hp_log` `hp_log10` `hp_pow` `hp_sin` `hp_cos` `hp_tan` `hp_asin` `hp_acos` `hp_atan` `hp_atan2` `hp_sinh` `hp_cosh` `hp_tanh` `hp_fabs` `hp_floor` `hp_ceil` `hp_trunc` `hp_round` `hp_fmod` `hp_frexp` `hp_modf` | 23 |

### 3.2 有 `.c` 但函数不全

| 头 | 缺口 | 缺失函数 |
|---|---|---|
| `hp_gfx.h` | 10/30 | `hp_pixel_a` `hp_triangle` `hp_fill_triangle` `hp_grob_w` `hp_grob_h` `hp_target_w` `hp_target_h` `hp_blit` `hp_blit_key` `hp_blit_scaled` |
| `hp_input.h` | 2/9 | `hp_input_hooked` `hp_input_busy` |
| `hp_string.h` | 4/31 | `ftoa` `dtoa` `strtod` `atof` |
| `prime.h` | 6/23 | `hp_printf8` `hp_sprintf8` `qsort` `bsearch` `abs` `labs` |

`hp_math.c` 只补了两处（`hp_ldexp`、旧版 `hp_double_to_str`）；其余 `hp_*` 数学函数见 §3.1。

### 3.3 依赖的外部符号（须由 L2 或应用提供）

| 符号 | 提供者 |
|---|---|
| `prime_sys_get_lcd` / `prime_sys_sleep` / `prime_sys_get_event` | `platform/prime_input.S` |
| `prime_privileged_memcpy` | `platform/prime_input.S` |
| `prime_hook_install` / `prime_hook_remove` / `prime_hook_active` | `src/prime_hook.c` |

`hp_rt.c` 还引用了 `hp_svc_*`（`rt_svc.o`，源码 `platform/rt_svc.s` **缺失**）。

### 3.4 契约不一致（头 vs 实现，只标记不改）

- `prime.h` 声明 `hp_printf8`/`hp_sprintf8`（C99 变参宏的派发目标，最多 8 个值），
  而 `hp_rt.c` 只定义了 **4 参** 的 `printf`/`sprintf`；
- `hp_image.h` 声明的 `hp_blit*` 归 `hp_gfx`（本模块不重复实现）；
- `hp_random.h` 的 `hp_fx_int` 属 `hp_fixmath`；
- 现有 `src/*.c` 是**旧版**，与较新的 `*.h` 不完全对应（如 `hp_gfx.c` 未实现
  `hp_gfx.h` 声明的 `hp_blit*`）。补齐时以**头文件契约**为准。

### 3.5 既知点（详见 prime-tcc/STATUS.md §3）

`hp_gfx.h`/`hp_fonts.h`/`hp_string.h` 的若干注释与实现描述不一致；`hp_fonts.h`
内部的字体名（Cascadia Code / Unifont）也不自洽。这些**只记录、未修改**。

## 4. 取事件的正确方式

`prime_sys_get_event` 走 **SVC #0x1003f**，**不能在普通循环里轮询**——只在固件的事件
分发路径中有效。正确做法（与 PureDOOM / suika 一致）：用 `prime_hook_install()` 把固件
入口 `0x307FBFA0` 换成指向你回调的 trampoline，**回调内自行调用 `prime_sys_get_event()`**。

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

细节见 [`docs/hook_abi.md`](docs/hook_abi.md)。`app-collection` 的 `suika` 与 `cube3d`
均按此方式使用。

## 5. 复核命令

```bash
# 逐头核对"声明但无实现"（§3.1/§3.2）
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
```

## 6. 维护约定

1. 本目录是 C 核心库的**唯一权威源**，改动会影响 `prime-tcc` 与 `app-collection` 全部工程；
2. 补实现时**以头文件契约为准**，并同步更新本文件的缺漏清单；
3. 契约与 ABI 约定见 [`../../prime-tcc/API_ABI_CONTRACT.md`](../../prime-tcc/API_ABI_CONTRACT.md)；
4. 分层调整（新增/移动文件）后，同步 `toolchain/templates/app.mk`、
   `toolchain/examples/api-probe/Makefile` 与 `prime-tcc/Makefile` 的路径。
