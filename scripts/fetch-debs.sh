#!/usr/bin/env bash
#
# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
#
# fetch-debs.sh —— 开箱即用：从 apt 源获取工具链所需的 .deb
#
# 背景：`armtc/*.deb` 合计约 600MB（单个最大 486MB），超出托管限制，因此不入库。
# 本脚本从**当前 apt 源**下载同名包，随后 `make setup` 即可完成安装。
#
# 注意：apt 源里通常只有**最新版**，可能与 VERSIONS.md 记录的基线版本不同。
# 若需要与基线逐字节一致的版本，请向维护者索取对应的 .deb（VERSIONS.md §1 附 SHA256）。
#
# 用法：
#   scripts/fetch-debs.sh              # 下载到 armtc/
#   scripts/fetch-debs.sh --check      # 只检查/报告，不下载
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TC="$(cd "$HERE/.." && pwd)"
DEBDIR="$TC/armtc"

PKGS=(
  binutils-arm-none-eabi
  gcc-arm-none-eabi
  libnewlib-arm-none-eabi
  libnewlib-dev
  libstdc++-arm-none-eabi-dev
  libstdc++-arm-none-eabi-newlib
)

CHECK_ONLY=0
[ "${1:-}" = "--check" ] && CHECK_ONLY=1

log()  { printf '\033[1;34m[fetch]\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[ ok  ]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[warn ]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[fail ]\033[0m %s\n' "$*" >&2; exit 1; }

# ---- 前置检查 ----
if ! command -v apt-get >/dev/null 2>&1; then
  die "需要 Debian/Ubuntu（apt-get）。其他发行版请改用 toolchain/README.md 的方式 B/C。"
fi

log "目标目录：$DEBDIR"
log "需要的包：${#PKGS[@]} 个"

have=0
for p in "${PKGS[@]}"; do
  if compgen -G "$DEBDIR/${p}_*.deb" >/dev/null 2>&1; then
    have=$((have + 1))
  fi
done
log "已存在：$have/${#PKGS[@]}"

if [ "$CHECK_ONLY" = 1 ]; then
  for p in "${PKGS[@]}"; do
    if compgen -G "$DEBDIR/${p}_*.deb" >/dev/null 2>&1; then
      ok "$p"
    else
      printf '  缺失  %s\n' "$p"
    fi
  done
  exit 0
fi

mkdir -p "$DEBDIR"
cd "$DEBDIR"

missing=0
for p in "${PKGS[@]}"; do
  if compgen -G "${p}_*.deb" >/dev/null 2>&1; then
    log "已存在，跳过：$p"
    continue
  fi
  log "下载 $p"
  if ! apt-get download "$p"; then
    warn "无法下载 $p（apt 索引可能陈旧，可先运行：sudo apt-get update）"
    missing=$((missing + 1))
  fi
done

echo
if [ "$missing" -eq 0 ]; then
  ok "全部就绪。下一步：make setup && make verify"
else
  die "$missing 个包未取到；补齐后重试，或按 README 的方式 B/C 安装。"
fi
