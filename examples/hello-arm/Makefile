# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
#
# toolchain/examples/hello-arm/Makefile —— 接口性示例（工具链最小闭环）
#
#   make check   交叉编译 + 链接 + 检查 ELF 架构（默认）
#   make obj     只编译（freestanding 目标文件）
#   make elf     只链接（newlib + nosys）
#   make clean
#
# 前置：source toolchain/scripts/env.sh（ARM 侧必须经 armtc-tools 包装层调用）
#
# 注意 ARMCC 不能用 CC：CC 是 make 内置变量，`CC ?=` 不会生效。

ARMCC   ?= arm-none-eabi-gcc
READELF ?= arm-none-eabi-readelf
ARCH    := -mcpu=arm926ej-s -marm -mfloat-abi=soft

.PHONY: all obj elf check clean

all: check

obj:
	@$(ARMCC) $(ARCH) -ffreestanding -c -o hello.o hello.c
	@echo "--- hello.o（freestanding 目标文件）---"
	@$(READELF) -h hello.o | grep -E 'Machine|Flags'

elf:
	@$(ARMCC) $(ARCH) --specs=nosys.specs -o hello.elf hello.c 2>&1 \
	    | grep -v -e 'is not implemented and will always fail' -e 'libc\.a(libc_a-' || true
	@test -f hello.elf || { echo "链接失败"; exit 1; }
	@echo "--- hello.elf（newlib 链接）---"
	@$(READELF) -h hello.elf | grep -E 'Class|Type|Machine|Flags'

check: obj elf
	@echo "工具链最小闭环通过 ✓（预期警告 _close/_read… 来自 nosys 空桩）"

clean:
	rm -f hello.o hello.elf
