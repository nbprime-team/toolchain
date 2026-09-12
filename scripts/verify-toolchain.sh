#!/usr/bin/env bash
#
# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
#
# verify-toolchain.sh —— 端到端验证仓库内 ARM 交叉工具链是否可用
#
# 验证内容：
#   1. 必需工具存在且可执行（gcc/g++/as/ld/ar/objdump/readelf/nm/strip/size）
#   2. 裸名入口解析正确（as/ld 必须命中 armtc-tools，而不是宿主机 x86 工具）
#   3. freestanding 编译 -> ELF32 / ARM / EABI5 / soft-float 目标文件
#   4. 汇编 -> ar 归档 -> nm 符号检查
#   5. newlib 链接（--specs=nosys.specs）-> 可执行 ELF，机器码为 ARM
#   6. newlib 头文件可解析（<stdio.h>）
#
# 退出码：0 = 全部通过；1 = 存在失败项。
# 所有中间产物写在临时目录，不污染工作区。
#
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN_DIR="$(cd "$HERE/.." && pwd)"

PASS=0
FAIL=0
declare -a FAILED_ITEMS=()

c_ok()   { printf '\033[1;32m  PASS\033[0m  %s\n' "$*"; PASS=$((PASS + 1)); }
c_fail() { printf '\033[1;31m  FAIL\033[0m  %s\n' "$*"; FAIL=$((FAIL + 1)); FAILED_ITEMS+=("$*"); }
c_info() { printf '\033[1;36m  ----\033[0m  %s\n' "$*"; }
c_head() { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }

# 以子 shell 的隔离方式启用工具链，避免污染调用者环境
TC_ROOT="$TOOLCHAIN_DIR/armtc/root"
TC_TOOLS="$TOOLCHAIN_DIR/armtc-tools"
export TC_ROOT TC_TOOLS
export PATH="$TC_TOOLS:$TC_ROOT/usr/bin:$PATH"

c_head "工具链布局"
if [ ! -x "$TC_ROOT/usr/bin/arm-none-eabi-gcc" ]; then
  c_fail "缺少 $TC_ROOT/usr/bin/arm-none-eabi-gcc（请先运行 scripts/setup-toolchain.sh）"
  echo; echo "结果：无法继续验证。"; exit 1
fi
c_ok "TC_ROOT = $TC_ROOT"
c_info "TC_TOOLS = $TC_TOOLS"

# ---------------------------------------------------------------- 1. 工具 --
c_head "1. 必需工具可用性"
for t in gcc g++ as ld ar objdump readelf nm strip size; do
  if "$TC_ROOT/usr/bin/arm-none-eabi-$t" --version >/dev/null 2>&1; then
    c_ok "arm-none-eabi-$t"
  else
    c_fail "arm-none-eabi-$t 不可执行"
  fi
done

if [ -x "$TC_TOOLS/arm-none-eabi-gcc" ]; then
  c_ok "包装层 arm-none-eabi-gcc 存在"
else
  c_fail "包装层 $TC_TOOLS/arm-none-eabi-gcc 缺失（运行 scripts/setup-toolchain.sh --tools-only）"
fi

# ------------------------------------------------------- 2. 裸名入口解析 --
c_head "2. 裸名入口解析（防止回落到宿主机工具）"
for t in as ld ar; do
  resolved="$(command -v "$t" 2>/dev/null || true)"
  if [ "$resolved" = "$TC_TOOLS/$t" ]; then
    c_ok "$t -> $resolved"
  else
    c_fail "$t 解析为 '${resolved:-<未找到>}'，期望 $TC_TOOLS/$t"
  fi
done

# 包装层必须“自足”：GCC driver 按裸名找 as，collect2 按 target 前缀找 ld。
# 只把 armtc-tools 放进 PATH（不带工具链 bin 目录）也必须能编译并通过链接。
c_head "2b. 包装层自足性（PATH 仅含 armtc-tools）"
SELFTEST="$(mktemp -d)" || c_fail "无法创建自足性测试目录"
if [ -n "${SELFTEST:-}" ]; then
  cat > "$SELFTEST/a.c" <<'EOF'
int main(void) { return 0; }
EOF
  if env -i PATH="$TC_TOOLS" HOME="$HOME" \
       "$TC_TOOLS/arm-none-eabi-gcc" -mcpu=arm926ej-s -marm -mfloat-abi=soft \
       -nostdlib -o "$SELFTEST/a.elf" "$SELFTEST/a.c" 2>"$SELFTEST/err.log"; then
    if arm-none-eabi-readelf -h "$SELFTEST/a.elf" | grep -q 'Machine: *ARM'; then
      c_ok "仅 armtc-tools 入 PATH 即可完成编译+链接"
    else
      c_fail "自足性测试产物架构不符"
    fi
  else
    c_fail "自足性测试失败（PATH 仅含 armtc-tools 时无法编译/链接）"
    sed 's/^/         /' "$SELFTEST/err.log" >&2
  fi
  rm -rf "$SELFTEST"
fi

# 更强的要求：用绝对路径调用包装脚本、且 PATH 里只有宿主机目录（含 x86 的
# as/ld）时也必须正确工作——包装脚本要自己把 armtc-tools 放进 PATH。
c_head "2c. 包装层 PATH 自注入（PATH 只有宿主目录）"
SELFTEST2="$(mktemp -d)" || c_fail "无法创建测试目录"
if [ -n "${SELFTEST2:-}" ]; then
  cat > "$SELFTEST2/a.c" <<'EOF'
int main(void) { return 0; }
EOF
  if env -i PATH="/usr/bin:/bin" HOME="$HOME" \
       "$TC_TOOLS/arm-none-eabi-gcc" -mcpu=arm926ej-s -marm -mfloat-abi=soft \
       -nostdlib -o "$SELFTEST2/a.elf" "$SELFTEST2/a.c" 2>"$SELFTEST2/err.log"; then
    if "$TC_TOOLS/readelf" -h "$SELFTEST2/a.elf" | grep -q 'Machine: *ARM'; then
      c_ok "仅用绝对路径调用包装脚本（PATH 无 armtc-tools）也能编译+链接"
    else
      c_fail "产物架构不符"
    fi
  else
    c_fail "PATH 自注入失效（会回落到宿主机 x86 工具）"
    sed 's/^/         /' "$SELFTEST2/err.log" >&2
  fi
  rm -rf "$SELFTEST2"
fi

# ------------------------------------------------------------- 3. 编译 ------
TMP="$(mktemp -d)" || { c_fail "无法创建临时目录"; echo; echo "结果：无法继续验证。"; exit 1; }
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || { c_fail "无法进入临时目录 $TMP"; echo; echo "结果：无法继续验证。"; exit 1; }

cat > hello.c <<'EOF'
#include <stdio.h>
int main(void) { printf("nbprime toolchain ok: %d\n", 42); return 0; }
EOF
echo "nop" > nop.s

c_head "3. freestanding 目标文件（-c）"
if arm-none-eabi-gcc -mcpu=arm926ej-s -marm -mfloat-abi=soft \
     -ffreestanding -c -o hello.o hello.c 2>"$TMP/gcc.log"; then
  hdr="$(arm-none-eabi-readelf -h hello.o)"
  if grep -q 'Machine: *ARM' <<<"$hdr" && grep -q 'ELF32' <<<"$hdr"; then
    c_ok "生成 ELF32/ARM 目标文件 hello.o"
    c_info "$(grep -E 'Type:|Machine:|Flags:' <<<"$hdr" | tr -s ' ' | tr '\n' ';')"
  else
    c_fail "目标文件架构不符：$(grep -E 'Machine:' <<<"$hdr")"
  fi
else
  c_fail "freestanding 编译失败"
  sed 's/^/         /' "$TMP/gcc.log" >&2
fi

c_head "4. 汇编 + ar 归档"
if arm-none-eabi-as -mcpu=arm926ej-s -o nop.o nop.s 2>"$TMP/as.log" \
   && arm-none-eabi-ar rcs libnop.a nop.o 2>>"$TMP/as.log" \
   && arm-none-eabi-nm libnop.a >/dev/null 2>&1; then
  c_ok "as + ar + nm 流程通过（libnop.a, $(stat -c%s libnop.a 2>/dev/null || echo '?') 字节）"
else
  c_fail "汇编/归档流程失败"
  sed 's/^/         /' "$TMP/as.log" >&2
fi

# --------------------------------------------------------- 5. newlib 链接 ---
c_head "5. newlib 链接（--specs=nosys.specs）"
if arm-none-eabi-gcc -mcpu=arm926ej-s -marm -mfloat-abi=soft \
     --specs=nosys.specs -o hello.elf hello.c 2>"$TMP/link.log"; then
  hdr="$(arm-none-eabi-readelf -h hello.elf)"
  if grep -q 'Machine: *ARM' <<<"$hdr" \
     && grep -q 'Version5 EABI' <<<"$hdr" \
     && grep -q 'soft-float ABI' <<<"$hdr"; then
    c_ok "链接出可执行 ELF（$(grep -E 'Type:' <<<"$hdr" | tr -s ' ')）"
    c_info "$(grep -E 'Flags:' <<<"$hdr" | tr -s ' ')"
  else
    c_fail "ELF 属性不符（期望 ARM / Version5 EABI / soft-float）"
    grep -E 'Machine:|Flags:' <<<"$hdr" >&2
  fi
else
  c_fail "newlib 链接失败"
  sed 's/^/         /' "$TMP/link.log" >&2
fi

c_head "6. newlib 头文件解析"
if echo '#include <stdio.h>
#include <string.h>
#include <math.h>' | arm-none-eabi-gcc -mcpu=arm926ej-s -marm -mfloat-abi=soft \
     -E -x c - >/dev/null 2>"$TMP/hdr.log"; then
  c_ok "stdio.h / string.h / math.h 可解析"
else
  c_fail "newlib 头文件解析失败"
  sed 's/^/         /' "$TMP/hdr.log" >&2
fi

# ---------------------------------------------------------------- 汇总 -----
c_head "版本摘要"
for t in gcc as ld; do
  printf '  %-4s %s\n' "$t" "$("$TC_ROOT/usr/bin/arm-none-eabi-$t" --version 2>/dev/null | head -1)"
done

printf '\n\033[1m结果：%d 项通过，%d 项失败\033[0m\n' "$PASS" "$FAIL"
if [ "$FAIL" -ne 0 ]; then
  printf '失败项：\n'
  for item in "${FAILED_ITEMS[@]}"; do printf '  - %s\n' "$item"; done
  exit 1
fi
exit 0
