#!/usr/bin/env bash
# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
#
# env.sh —— 启用仓库内本地 ARM 交叉工具链（需 **bash** source；zsh/sh 请改用
# `eval "$(make -C <repo>/toolchain env)"`）
#
# 用法：
#   source toolchain/scripts/env.sh      # 在当前 shell 中导出（bash）
#   eval "$(toolchain/scripts/env.sh)"   # 直接执行时打印可 eval 的语句
#   eval "$(make -C toolchain env)"      # 等价的 Makefile 入口（不要求 bash）
#
# 导出变量：
#   TC_ROOT        解包后的工具链根（armtc/root）
#   TC_BIN         交叉工具二进制目录
#   TC_TOOLS       包装层目录（裸名 as/ld + 注入参数的 gcc/g++）
#   CROSS_COMPILE  arm-none-eabi- 前缀，供 Makefile 使用
#   TOOLCHAIN_ROOT 同 TC_ROOT；供下游 Makefile（如 prime-tcc）定位 newlib 头
#
# 说明：务必把 TC_TOOLS 放在 PATH 最前。GCC driver 按“裸名”（as、ld）在
# PATH 中查找子程序；若让它先命中宿主机的 x86 as/ld，就会出现
# "invalid -march= option: armv5tej" 之类的致命错误。

if [ -z "${BASH_VERSION:-}" ]; then
  echo "env.sh 需要 bash（当前 shell 不支持 BASH_SOURCE；zsh/sh 请用 make env）。" >&2
  echo "可改用：eval \"\$(make -C <repo>/toolchain env)\"" >&2
  return 1 2>/dev/null || exit 1
fi

_tc_self="${BASH_SOURCE[0]:-$0}"
_tc_dir="$(cd "$(dirname "$_tc_self")" && pwd)"
_tc_root="$(cd "$_tc_dir/.." && pwd)"

TC_ROOT="$_tc_root/armtc/root"
TC_TOOLS="$_tc_root/armtc-tools"
TC_BIN="$TC_ROOT/usr/bin"

export TC_ROOT TC_TOOLS TC_BIN
# 下游工程（如 prime-tcc）用 TOOLCHAIN_ROOT 定位 newlib 头，二者指向同一目录
export TOOLCHAIN_ROOT="$TC_ROOT"
export CROSS_COMPILE="arm-none-eabi-"

# TC_TOOLS 必须排在最前（裸名 as/ld 必须优先于任何同类工具）；
# 先补 TC_BIN 到尾部，再把 TC_TOOLS 插到最前，避免顺序被下一次插入颠倒。
case ":$PATH:" in
  *":$TC_BIN:"*) ;;
  *) PATH="$PATH:$TC_BIN" ;;
esac
case ":$PATH:" in
  *":$TC_TOOLS:"*) ;;
  *) PATH="$TC_TOOLS:$PATH" ;;
esac
export PATH

unset _tc_self _tc_dir _tc_root

# 直接执行（非 source）时，输出可 eval 的语句
if [ "${BASH_SOURCE[0]:-}" = "$0" ]; then
  printf 'export TC_ROOT=%q\n' "$TC_ROOT"
  printf 'export TC_TOOLS=%q\n' "$TC_TOOLS"
  printf 'export TC_BIN=%q\n' "$TC_BIN"
  printf 'export TOOLCHAIN_ROOT=%q\n' "$TOOLCHAIN_ROOT"
  printf 'export CROSS_COMPILE=%q\n' "$CROSS_COMPILE"
  printf 'export PATH=%q\n' "$PATH"
fi
