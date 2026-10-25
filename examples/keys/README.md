# keys —— 原生按键示例（工具链接口示例）

> ⚠️ **AI 生成 / 辅助创作：DeepSeek V4.1 Flash**（未人工审查）

**用途**：用最小程序核对「nbprime 原生按键处理」这条链路。屏幕上直接显示**原始字段**，
便于逐键对照 [`../../sdk/docs/hook_abi.md`](../../sdk/docs/hook_abi.md)。

## 显示什么

| 行 | 含义 | ABI |
|---|---|---|
| `ty4=` | 事件大类（按键 = `00100010`） | `*(u32*)(ev+4)` |
| `w7 =` | 按下 `00000010` / 抬起 `00100000` | `*(u32*)(ev+28)` |
| `w8 =` | 高 16 位 = **物理扫描码** | `*(u32*)(ev+32)` |
| 大字 | 扫描码（十六进制，两位） | `w8 >> 16` |
| `last:` | 最近 8 个扫描码 | — |
| 底部 | 累计事件数 | — |

## 怎么核对

1. 按 **ESC（0x01）**、**ON（0x83）**、**ENTER（0x0D）**、**7/Q（0x51）** 等已知键；
2. 看大字扫描码是否与 [`../../sdk/include/hp_input.h`](../../sdk/include/hp_input.h)
   的 `HP_KEY_*` 一致；
3. 若 `ty4`/`w7` 与上表不符，说明该固件的事件布局与 `hook_abi.md` 有出入——**以实测为准**。

## 构建 / 部署

```bash
source ../../scripts/env.sh
make            # -> keys.elf
make deploy     # -> keys.hpappdir/
```

把 `keys.hpappdir/` 拷入计算器 `C:\DATA\` 运行（`my_app.elf` + `main.py` + 元数据）。

## 退出

ESC / ON。

## 实现要点

- 输入：`toolchain/sdk/src/prime_hook.c`（装 `0x307FBFA0` 钩子）+ `platform/prime_input.S`（SVC 0x1003f）；
- 公共运行支撑：`../app-common`；显示用**内置 8x8 十六进制字模**，不依赖字体资源；
- 满足两项 ELF 硬要求（`app_elf_requirements@0x0` + `main@0x14`）。
