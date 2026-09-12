# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
#
# toolchain/Makefile —— 本地 ARM 交叉工具链的统一入口
#
# 常用：
#   make setup    从 armtc/*.deb 重建工具链（解包 + 修权限 + 生成包装层）
#   make verify   端到端验证工具链（编译/汇编/链接/ELF 架构检查）
#   make env      打印可 eval 的环境变量
#   make versions 打印实测版本矩阵
#   make host-check 校验主机（PC）工具链基线
#   make clean-tools  删除可再生的包装层 armtc-tools/
#
# 注意：本目录下的 armtc/ 与 armtc-tools/ 均为本地生成物，不应提交到
# 版本控制（见 .gitignore）。工具链本体体积约 4GB。

TOOLCHAIN_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
SCRIPTS       := $(TOOLCHAIN_DIR)/scripts
ROOT          := $(TOOLCHAIN_DIR)/armtc/root
TOOLS         := $(TOOLCHAIN_DIR)/armtc-tools
GCC           := $(ROOT)/usr/bin/arm-none-eabi-gcc

.PHONY: all setup tools unpack-only verify env versions host-check clean-tools help

all: verify

help:
	@sed -n '3,14p' $(lastword $(MAKEFILE_LIST))

host-check:
	@$(SCRIPTS)/check-host.sh

setup:
	@$(SCRIPTS)/setup-toolchain.sh

tools:
	@$(SCRIPTS)/setup-toolchain.sh --tools-only

unpack-only:
	@$(SCRIPTS)/setup-toolchain.sh --unpack-only

verify:
	@$(SCRIPTS)/verify-toolchain.sh

env:
	@printf 'export TC_ROOT=%s\n' '$(ROOT)'
	@printf 'export TC_TOOLS=%s\n' '$(TOOLS)'
	@printf 'export TC_BIN=%s\n' '$(ROOT)/usr/bin'
	@printf 'export TOOLCHAIN_ROOT=%s\n' '$(ROOT)'
	@printf 'export CROSS_COMPILE=%s\n' 'arm-none-eabi-'
	@printf 'export PATH=%s\n' '$(TOOLS):$(ROOT)/usr/bin:$$PATH'

versions:
	@printf '%-24s %s\n' 'artifact' 'version'
	@printf '%-24s %s\n' '--------' '-------'
	@if [ -x "$(GCC)" ]; then \
	  for t in gcc g++ as ld ar objdump readelf nm strip size; do \
	    v="$$("$(ROOT)/usr/bin/arm-none-eabi-$$t" --version 2>/dev/null | head -1)"; \
	    printf '%-24s %s\n' "arm-none-eabi-$$t" "$$v"; \
	  done; \
	else \
	  echo "工具链未解包：请先 make setup"; exit 1; \
	fi

clean-tools:
	@rm -rf "$(TOOLS)"
	@echo "已删除可再生的包装层 $(TOOLS)（make tools 可重建）"
