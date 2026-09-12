# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
#
# toolchain/examples/api-probe/Makefile —— 接口示例（API/ABI 探针）
#
#   make check   在 PC 与 ARM 两端编译同一份 probe.c（默认目标）
#   make pc      仅宿主 gcc
#   make arm     仅仓库内交叉工具链
#   make clean
#
# 前置：ARM 侧需要工具链在 PATH 中，先执行
#   source toolchain/scripts/env.sh

API_DIR   ?= $(abspath $(CURDIR)/../../../prime-tcc)
CC_PC     ?= cc
CC_ARM    ?= arm-none-eabi-gcc
READELF   ?= arm-none-eabi-readelf

WARN      := -Wall -Wextra
INCLUDES  := -I$(API_DIR)/hp -I$(API_DIR)/rt
ARM_FLAGS := -mcpu=arm926ej-s -marm -mfloat-abi=soft -ffreestanding

.PHONY: all check pc arm clean

all: check

pc: probe_pc.o

arm: probe_arm.o

probe_pc.o: probe.c
	$(CC_PC) $(WARN) $(INCLUDES) -c -o $@ $<

probe_arm.o: probe.c
	$(CC_ARM) $(WARN) $(ARM_FLAGS) $(INCLUDES) -c -o $@ $<

check: probe_pc.o probe_arm.o
	@echo "== 主机（PC gcc）：probe_pc.o =="
	@echo "== 目标（ARM gcc）：probe_arm.o =="
	@$(READELF) -h probe_arm.o 2>/dev/null | grep -E 'Machine|Flags' || true
	@echo "两端编译通过；ABI 断言已在编译期检查（见 probe.c 顶部）。"
	@echo "注意：本示例只做编译验证，不链接（两侧 hp_* 实现见 prime-tcc/STATUS.md）。"

clean:
	rm -f probe_pc.o probe_arm.o
