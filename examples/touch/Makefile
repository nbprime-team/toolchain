# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
#
# toolchain/examples/touch/Makefile —— 原生触摸示例
#
#   make            编译 -> touch.elf
#   make check      readelf 检查 ELF 形状
#   make deploy     产出可上传包 touch.hpappdir/
#   make clean

# toolchain/examples/touch -> toolchain/
SDK    ?= ../..
TARGET := touch
APPDIR := $(TARGET).hpappdir

OBJS   := $(TARGET).o prime_input.o prime_hook.o

include $(SDK)/templates/app.mk
include $(SDK)/examples/app-common/app_common.mk
