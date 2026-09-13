# toolchain

> ⚠️ **AI 生成 / 辅助创作：DeepSeek V4.1 Flash**（LCD 已人工审查）

Nbprime 组织的**基础设施层**：为 HP Prime G1（ARM926EJ-S / ARMv5TEJ，软浮点）
提供**离线、可复现**的交叉编译环境，并以 `sdk/` 承载所有 Prime 用户程序的
**C 核心库**。不承载业务代码。版本基线与校验和见 [VERSIONS.md](VERSIONS.md)。

## 快速开始（开箱即用）

```bash
cd toolchain
make bootstrap     # 一条龙：获取 .deb（如需）→ setup → verify
source scripts/env.sh
```

`make bootstrap` 在缺少 `armtc/*.deb` 时会自动从 apt 源获取。

单独执行：

```bash
make fetch-debs    # 仅获取缺失的 .deb
make setup         # 解包 + 修权限 + 生成包装层
make verify        # 端到端验证：21 项，全通过才算可用
make host-check    # 主机（PC）工具链基线：宿主 gcc/make/python3/binutils
```

## 安装（三选一）

| 方式 | 做法 | 适用 |
|---|---|---|
| **A. 仓库内置**（推荐，开箱即用） | `make bootstrap`（缺 `.deb` 时自动从 apt 源获取） | 与基线一致 |
| B. 系统包 | `apt install gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi libnewlib-dev` | 快速试编译；版本低于基线 |
| C. ARM 官方工具链 | 解压官方 tarball 并加 `bin/` 到 `PATH` | 自洽布局，无需包装层 |

方式 B/C 的注意点：

- 方式 B 的 newlib 头路径随发行版而异，用
  `dpkg -L libnewlib-dev | grep -m1 'stdio\.h'` 确认后再决定 `-isystem`；
- 方式 C 不需要 `-isystem`/`-B`/`-L` 补偿，也不需要包装层。

## 目录结构

```text
toolchain/
├── README.md / VERSIONS.md      # 本文件 / 版本基线与实测记录
├── Makefile                     # setup / verify / host-check / env / versions / clean-tools
├── .gitignore                   # 忽略解包产物、.deb 缓存与生成的包装层
├── scripts/
│   ├── setup-toolchain.sh       # 解包 .deb + 修权限 + 生成包装层
│   ├── verify-toolchain.sh      # 21 项端到端验证
│   ├── check-host.sh            # 主机（PC）工具链基线
│   └── env.sh                   # 环境导出（bash source 或 make env）
├── examples/
│   ├── api-probe/               # 接口示例：API/ABI 探针（PC 与 ARM 两端编译）
│   ├── hello-arm/               # 接口示例：工具链最小闭环
│   └── app-common/              # 应用公共运行支撑（app-collection 的工程 include 它）
├── templates/
│   └── app.mk                   # 应用 Makefile 的公共片段（app-collection 的工程 include 它）
├── sdk/                         # C 核心库（唯一权威源），按抽象层次分三层：
│   ├── include/                 #   L0 契约层：公共头（prime.h、stdbool.h、hp_*.h、prime_hook.h）
│   ├── src/                     #   L1 实现层：hp_*.c、prime_hook.c
│   ├── platform/                #   L2 平台层：prime_input.S（SVC 包装）、prime_dyn.ld（链接脚本）
│   └── docs/                    #   文档：hook_abi.md
├── armtc/                       # *.deb（缓存）+ root/（解包结果）
└── armtc-tools/                 # 生成的包装层（不入库，make tools 可重建）
```

## 核心设计：为什么必须经 `armtc-tools/` 包装层

本工具链是 **Debian 打包版**，解包到非 `/usr` 前缀后**不能直接使用**：

| 问题 | 现象 | 解决 |
|---|---|---|
| GCC driver 按**裸名** `as` 查 `PATH` | 命中宿主机 x86 `as` → `invalid -march= option: armv5tej` | 包装层提供裸名入口 |
| `collect2` 按**前缀名** `arm-none-eabi-ld` 查 `PATH` | 编译正常、链接报 `collect2: fatal error: cannot find 'ld'` | 包装层同时提供带前缀入口 |
| newlib 头在 `usr/include/newlib/`（非 sysroot 位置） | `fatal error: stdio.h: No such file or directory` | 包装脚本注入 `-isystem` |
| `nosys.specs` / multilib 库需显式路径 | `cannot read spec file 'nosys.specs'` / 找不到 `libc.a` | 包装脚本注入 `-B`、`-L` |
| 子程序只认 `PATH`（即使写了绝对路径） | 仅用绝对路径调用时回落 x86 `as`/`ld` | 包装脚本把自身目录注入 `PATH` 最前 |

**结论**：通过 `armtc-tools/arm-none-eabi-gcc`（或 `source scripts/env.sh` 后的
`arm-none-eabi-gcc`）调用编译器；包装层自足，只需把它放进 `PATH` 即可完成
「编译 + 链接」（`make verify` 第 2b/2c 项即检验这两点）。

## Makefile 目标

| 目标 | 作用 |
|---|---|
| `make setup` / `make setup --force-unpack` | 解包（可选强制）+ 修权限 + 生成包装层 |
| `make tools` | 只重建包装层 |
| `make verify` | 21 项验证；有失败即非零退出 |
| `make host-check` | 主机（PC）工具链基线（脚本内自动排除 `armtc-tools`，可在 `source env.sh` 后直接跑） |
| `make env` / `make versions` | 打印可 eval 的环境 / 版本矩阵 |
| `make clean-tools` | 删除可再生的包装层 |

## C 核心库（`sdk/`）

`sdk/` 是所有 Prime 用户程序的公共构建件（详见 [sdk/README.md](sdk/README.md)），
按抽象层次分三层 + 文档：

| 层 | 目录 | 内容 |
|---|---|---|
| L0 契约层 | `include/` | `prime.h`、`stdbool.h`、`prime_hook.h`、`hp_*.h` |
| L1 实现层 | `src/` | `hp_rt.c`、`hp_string.c`、`hp_math.c`、`hp_gfx.c`、`hp_input.c`、`prime_hook.c` |
| L2 平台层 | `platform/` | `prime_input.S`（SVC 包装）、`prime_dyn.ld`（链接脚本） |
| 文档 | `docs/` | `hook_abi.md` |

引用：`-I$(SDK)/sdk/include`、`-T$(SDK)/sdk/platform/prime_dyn.ld`。

> ⚠️ 各模块的实现缺漏（108 个 `hp_*` 函数、多个整模块无 `.c`）已逐条登记在
> [sdk/README.md §3](sdk/README.md)。

## 接口示例（API/ABI 探针）

`examples/api-probe/` 用**同一份 `probe.c`** 在宿主 gcc 与 ARM 交叉 gcc 下编译，
覆盖 C 核心库的全部 13 个公开头文件并做编译期 ABI 断言：

```bash
source toolchain/scripts/env.sh
make -C toolchain/examples/api-probe check    # 两端应 0 警告通过
```

只做**编译**验证（不链接）：`hp_*` 的已就位实现在 `sdk/`，缺失部分见
[../prime-tcc/STATUS.md](../prime-tcc/STATUS.md)。

## 典型使用

```bash
source toolchain/scripts/env.sh
# freestanding 目标文件
arm-none-eabi-gcc -mcpu=arm926ej-s -marm -mfloat-abi=soft -ffreestanding -c -o x.o x.c
# 用 newlib 链接
arm-none-eabi-gcc -mcpu=arm926ej-s -marm -mfloat-abi=soft --specs=nosys.specs -o x.elf x.c
```

`nosys.specs` 的 `_close is not implemented...` 属**预期警告**（空 syscall 桩）。

## 依赖与已知限制

- 宿主：Debian/Ubuntu（`dpkg`；无则退回 `ar`+`tar`）、`make`、`bash`、`binutils`
- 目标工具链见 [VERSIONS.md §1](VERSIONS.md)；**不需要**宿主另装 `gcc-arm-none-eabi`
- 只覆盖 **ARMv5TEJ / 软浮点**（Prime G1）一组目标参数
- `source scripts/env.sh` 后 `PATH` 最前的 `armtc-tools/` 提供裸名 `as`/`ld`
  （ARM 版，GCC driver 按裸名查找所需），因此**宿主**编译（如直接 `gcc x.c`）
  会被劫持（`unrecognised emulation mode: elf_x86_64`）；`make host-check` 已在
  脚本内自动排除该目录，宿主编译请另开干净 PATH 的终端
- `armtc/root/`、`armtc/*.deb`、`armtc-tools/` 都是本地生成物（见 `.gitignore`）
- 工具链本身**不需要**网络

## 维护约定

1. 任何"可用/已修复"的结论必须附 `make verify` 的真实输出；
2. 升级工具链须同步更新 `VERSIONS.md` 的版本与 SHA256；
3. **不维护** `nbprime/`（已废弃，不作构建来源）；
4. 改包装层、目标参数或 `sdk/` 后，复验 `prime-tcc/`、`app-collection/`。
