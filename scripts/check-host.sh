#!/usr/bin/env bash
#
# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
#
# check-host.sh —— 主机（PC）工具链基线
#
# 用途：记录并校验**宿主**编译环境。目标环境的交叉工具链由 setup/verify 管理
# （见 README.md、VERSIONS.md）；本脚本只关心"在 PC 上编译同一份用户代码"
# 所需的最小环境，与 TCC/PC 的 API 统一契约配套（见
# ../prime-tcc/API_ABI_CONTRACT.md）。
#
# 退出码：0 = 必需工具齐备；1 = 有缺失。
# 输出为可直接粘贴进 VERSIONS.md 的表格行。
#
set -uo pipefail

PASS=0
FAIL=0
declare -a MISSING=()

row() { printf '| `%s` | %s |\n' "$1" "$2"; }
ok()  { PASS=$((PASS + 1)); }
bad() { FAIL=$((FAIL + 1)); MISSING+=("$1"); }

echo "| 工具 | 版本 / 取值 |"
echo "|---|---|"

# ---- 必需：宿主 C 编译器（编译用户代码、跑接口示例）----
CC_BIN=""
for c in cc gcc clang; do
  if command -v "$c" >/dev/null 2>&1; then CC_BIN="$c"; break; fi
done
if [ -n "$CC_BIN" ]; then
  ver="$("$CC_BIN" --version 2>/dev/null | head -1)"
  tgt="$("$CC_BIN" -dumpmachine 2>/dev/null || echo '?')"
  row "$CC_BIN (host C compiler)" "${ver:-?}；target ${tgt}"
  ok
else
  row "host C compiler" "**缺失**（需要 cc/gcc/clang）"
  bad "host C compiler"
fi

# ---- 必需：make ----
if command -v make >/dev/null 2>&1; then
  row "make" "$(make --version 2>/dev/null | head -1)"
  ok
else
  row "make" "**缺失**"
  bad "make"
fi

# ---- 必需：python3（示例里的 fontgen/fetch 脚本用）----
if command -v python3 >/dev/null 2>&1; then
  row "python3" "$(python3 --version 2>&1 | head -1)"
  ok
else
  row "python3" "**缺失**"
  bad "python3"
fi

# ---- 宿主 binutils（C 编译器依赖其 as/ld）----
for t in as ld ar objdump nm readelf objcopy; do
  if command -v "$t" >/dev/null 2>&1; then
    row "$t (host binutils)" "$("$t" --version 2>/dev/null | head -1)"
    ok
  else
    row "$t (host binutils)" "**缺失**"
    bad "$t"
  fi
done

# ---- 数据模型：PC 为 LP64，ARM 目标为 ILP32（见 API_ABI_CONTRACT.md §3）----
if [ -n "$CC_BIN" ]; then
  dm="$(printf '#include <stddef.h>\nint main(void){return (int)sizeof(size_t);}\n' \
        | "$CC_BIN" -x c - -o /dev/null -D_FILE_OFFSET_BITS=64 2>/dev/null && echo ok)"
  bits="$(printf '#include <limits.h>\n#include <stdio.h>\nint main(void){printf("%%d",(int)(sizeof(void*)*8));return 0;}\n' \
        | "$CC_BIN" -x c - -o /tmp/.hostbits.$$ 2>/dev/null && /tmp/.hostbits.$$ ; rm -f /tmp/.hostbits.$$)"
  row "数据模型" "指针宽度 ${bits:-?} 位（ARM 目标为 32 位）"
  [ -n "$bits" ] && ok || bad "数据模型探测"
fi

echo
if [ "$FAIL" -eq 0 ]; then
  printf '\033[1;32m主机工具链基线：%d 项齐备，0 项缺失\033[0m\n' "$PASS"
  exit 0
fi
printf '\033[1;31m主机工具链基线：%d 项齐备，%d 项缺失：%s\033[0m\n' \
  "$PASS" "$FAIL" "${MISSING[*]}"
exit 1
