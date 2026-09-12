#!/usr/bin/env bash
#
# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
#
# setup-toolchain.sh —— 从仓库内 .deb 重建本地 ARM 交叉工具链
#
# 为什么需要这个脚本：
#   仓库内 armtc/ 下的 .deb 解包后，GCC driver 会按编译期 prefix（/usr）去
#   查找 as/ld，而本工具链被解包到非 /usr 前缀，导致 GCC 找不到自带的
#   arm-none-eabi-as，回落到系统 PATH 上的同名工具（x86 的 as），报出
#   "invalid -march= option: armv5tej" 之类的致命错误。
#   同时 newlib 头文件位于 usr/include/newlib（Debian 打包布局）而不是
#   GCC 默认的 sysroot 位置，specs 与 multilib 库目录也需显式指出。
#   本脚本负责：解包 -> 修权限 -> 生成 armtc-tools 包装层。
#
# 用法：
#   scripts/setup-toolchain.sh                 # 已存在则只修权限并重建包装层
#   scripts/setup-toolchain.sh --force-unpack  # 强制重新解包全部 .deb
#   scripts/setup-toolchain.sh --unpack-only
#   scripts/setup-toolchain.sh --tools-only
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN_DIR="$(cd "$HERE/.." && pwd)"
DEB_DIR="$TOOLCHAIN_DIR/armtc"
ROOT="$DEB_DIR/root"
BIN="$ROOT/usr/bin"
GCC_INTERNAL="$ROOT/usr/lib/gcc/arm-none-eabi/14.2.1"
TOOLS="$TOOLCHAIN_DIR/armtc-tools"

FORCE_UNPACK=0
DO_UNPACK=1
DO_TOOLS=1

for arg in "$@"; do
  case "$arg" in
    --force-unpack) FORCE_UNPACK=1 ;;
    --unpack-only)  DO_TOOLS=0 ;;
    --tools-only)   DO_UNPACK=0 ;;
    -h|--help)      sed -n '3,20p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "未知参数: $arg" >&2; exit 2 ;;
  esac
done

log()  { printf '\033[1;34m[setup]\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[ ok  ]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[warn ]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[fail ]\033[0m %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- 解包 ------
unpack_deb() {
  local deb="$1"
  if command -v dpkg >/dev/null 2>&1; then
    dpkg -x "$deb" "$ROOT"
  else
    # 无 dpkg 时退回到 ar + tar（.deb 即 ar 归档，内含 data.tar.*）
    local tmp
    tmp="$(mktemp -d)" || { warn "无法创建临时目录"; return 1; }
    if ! ( cd "$tmp" && ar x "$deb" && tar -xf data.tar.* -C "$ROOT" ); then
      rm -rf "$tmp"
      die "ar/tar 解包失败：$(basename "$deb")"
    fi
    rm -rf "$tmp"
  fi
}

if [ "$DO_UNPACK" = 1 ]; then
  if [ ! -d "$DEB_DIR" ]; then
    die "未找到 $DEB_DIR（请从上游 toolchain 仓库获取 .deb 包）"
  fi
  shopt -s nullglob
  debs=( "$DEB_DIR"/*.deb )
  shopt -u nullglob
  [ "${#debs[@]}" -gt 0 ] || die "未找到 $DEB_DIR/*.deb"

  if [ -x "$BIN/arm-none-eabi-gcc" ] && [ "$FORCE_UNPACK" = 0 ]; then
    log "检测到已解包的 $BIN/arm-none-eabi-gcc，跳过解包（--force-unpack 可强制）"
  else
    mkdir -p "$ROOT"
    for deb in "${debs[@]}"; do
      log "解包 $(basename "$deb")"
      unpack_deb "$deb"
    done
    ok "解包完成：${#debs[@]} 个 .deb -> $ROOT"
  fi
fi

# -------------------------------------------------------------- 权限 --------
if [ -d "$BIN" ]; then
  log "修正可执行权限"
  chmod +x "$BIN"/arm-none-eabi-* 2>/dev/null || true
  if [ -d "$GCC_INTERNAL" ]; then
    for t in cc1 cc1plus collect2 lto1 lto-wrapper g++-mapper-server; do
      # 用 if 而不是 [ ] && cmd：后者作为循环体末命令时，条件为假会被
      # set -e 当成失败而静默中止脚本。
      if [ -f "$GCC_INTERNAL/$t" ]; then chmod +x "$GCC_INTERNAL/$t"; fi
    done
    if [ -d "$GCC_INTERNAL/install-tools" ]; then
      chmod +x "$GCC_INTERNAL/install-tools"/* 2>/dev/null || true
    fi
  fi
  [ -x "$BIN/arm-none-eabi-gcc" ] || die "解包后仍找不到可执行的 arm-none-eabi-gcc"
  ok "权限修正完成"
else
  [ "$DO_TOOLS" = 1 ] && die "未找到 $BIN，请先运行不带 --tools-only 的 setup"
fi

# ------------------------------------------------------- 包装层 armtc-tools --
# 目标：把工具链封成一个可直接放进 PATH 的目录。
#   * binutils 的“裸名”入口（as/ld/ar/...）-> 相对符号链接到 arm-none-eabi-*
#     这是 GCC driver 查找 as/ld 的关键：GCC 按裸名在 PATH 中查找子程序。
#   * arm-none-eabi-gcc / g++ -> 包装脚本，注入被 Debian 打包布局拆散的
#     头文件、specs 与 multilib 库搜索路径。
if [ "$DO_TOOLS" = 1 ]; then
  log "生成包装层 $TOOLS"
  mkdir -p "$TOOLS"

  BARE_TOOLS=(as ld ar nm objdump readelf strip objcopy ranlib size addr2line
              strings c++filt elfedit gprof gcov gcov-dump gcov-tool lto-dump)
  for t in "${BARE_TOOLS[@]}"; do
    src="arm-none-eabi-$t"
    [ -x "$BIN/$src" ] || continue
    # 两种入口都要，缺一不可：
    #   * 裸名（as/ld）—— GCC driver 按裸名在 PATH 中查找子程序；
    #   * 带前缀名（arm-none-eabi-ld）—— collect2 按 target 前缀查找链接器。
    # 只提供一种就会出现“用源头一步能过、链接时报 cannot find 'ld'”。
    ln -sfn "../armtc/root/usr/bin/$src" "$TOOLS/$t"
    ln -sfn "../armtc/root/usr/bin/$src" "$TOOLS/$src"
  done
  ok "binutils 入口：$(find "$TOOLS" -maxdepth 1 -type l | wc -l) 个链接（裸名 + arm-none-eabi- 前缀各一份）"

  NEWLIB_REL="usr/lib/arm-none-eabi/newlib"

  write_gcc_wrapper() {
    local name="$1" real="$2"
    cat > "$TOOLS/$name" <<EOF
#!/bin/sh
# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
# 由 scripts/setup-toolchain.sh 生成，请勿手工编辑。
# 包装 $real，补齐本地解包工具链缺失的搜索路径。
set -e
# 不依赖外部命令（dirname 等）：在极简 PATH 下也要能定位自身
HERE=\$(case "\$0" in */*) cd "\${0%/*}" && pwd ;; *) pwd ;; esac)
ROOT="\$HERE/../armtc/root"
# 把包装层自己放到 PATH 最前：GCC driver 按裸名查找 as/ld，collect2 按
# arm-none-eabi- 前缀查找链接器，两者都只认 PATH —— 不能依赖调用者已经
# 设置好 PATH，否则会命中宿主机的 x86 as/ld。
PATH="\$HERE:\$PATH"
export PATH
exec "\$ROOT/usr/bin/$real" \\
  -isystem "\$ROOT/usr/include/newlib" \\
  -B"\$ROOT/$NEWLIB_REL" \\
  -L"\$ROOT/$NEWLIB_REL/arm/v5te/softfp" \\
  "\$@"
EOF
    chmod +x "$TOOLS/$name"
  }
  write_gcc_wrapper arm-none-eabi-gcc arm-none-eabi-gcc
  write_gcc_wrapper arm-none-eabi-g++ arm-none-eabi-g++
  ok "GCC/G++ 包装脚本已生成"
fi

echo
ok "工具链就绪：$ROOT"
echo "  启用方式：  source \"$TOOLCHAIN_DIR/scripts/env.sh\""
echo "  验证方式：  make -C \"$TOOLCHAIN_DIR\" verify"
