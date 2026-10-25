# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
#
# toolchain/examples/keys/Makefile —— 原生按键示例
#
#   make            编译 -> keys.elf
#   make check      readelf 检查 ELF 形状
#   make deploy     产出可上传包 keys.hpappdir/
#   make clean
#
# 复用 toolchain 自身的公共件：templates/app.mk + examples/app-common。

# toolchain/examples/keys -> toolchain/
SDK    ?= ../..
TARGET := keys
APPDIR := $(TARGET).hpappdir

OBJS   := $(TARGET).o prime_input.o prime_hook.o

include $(SDK)/templates/app.mk
include $(SDK)/examples/app-common/app_common.mk
