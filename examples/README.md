# toolchain/examples —— 接口性示例

> ⚠️ **AI 生成 / 辅助创作：DeepSeek V4.1 Flash**（未人工审查）

本目录放**接口性示例**：用**最小程序**验证"工具链 / C API 契约"能否被正确使用。
与之相对，**工程性示例**（完整应用，有实际功能）在 [`app-collection/`](../../../app-collection/)。

| 示例 | 验证什么 | 命令 |
|---|---|---|
| [`api-probe/`](api-probe/) | **C API/ABI 契约**：同一份 `probe.c` 在 PC gcc 与 ARM gcc 下 **0 警告**编译，且编译期 ABI 断言成立（覆盖 `prime-tcc` 的 13 个公开头文件） | `make -C toolchain/examples/api-probe check` |
| [`hello-arm/`](hello-arm/) | **工具链最小闭环**：交叉编译 → 汇编 → 链接（newlib + nosys）→ 检查 ELF 架构 | `make -C toolchain/examples/hello-arm` |

两者的共同点：**只验证接口，不实现功能**；产物不落地（或落在临时目录）。
工程性示例（如 `app-collection/examples/suika`）才会产出真正可上机的 `.hpappdir`。

## 前置

```bash
make -C toolchain bootstrap      # 或 setup + verify
source toolchain/scripts/env.sh
```
