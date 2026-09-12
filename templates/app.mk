# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
#
# toolchain/templates/app.mk —— app-collection 应用 Makefile 的公共片段（唯一来源）。
#
# 用法（在应用的 Makefile 里）：
#
#   SDK    ?= ../../../toolchain
#   TARGET := cube3d
#   APPDIR := $(TARGET).hpappdir          # 定义它才生成 deploy 目标
#   OBJS   := $(TARGET).o prime_input.o prime_hook.o
#   include $(SDK)/templates/app.mk
#
# 可在 include 前覆盖的变量：
#   SDK / ARMCC / READELF / PYTHON / TARGET / APPDIR / OBJS /
#   ARCHFLAGS / CFLAGS / LDFLAGS / LDLIBS / EXTRA_CLEAN
#
# 注意：交叉编译器**不能**用 CC —— CC 是 make 的内置变量（默认 cc），
# `CC ?= arm-none-eabi-gcc` 不会生效，会静默退回宿主 cc 并报
# “unrecognized command-line option '-marm'”。故一律用 ARMCC。

SDK      ?= ../../../toolchain
ARMCC    ?= arm-none-eabi-gcc
READELF  ?= arm-none-eabi-readelf
PYTHON   ?= python3

TARGET   ?= my_app
OBJS     ?= $(TARGET).o prime_input.o

# 目标：HP Prime G1（ARM926EJ-S / ARMv5TEJ，软浮点）
ARCHFLAGS ?= -mcpu=arm926ej-s -marm -mfloat-abi=soft

CFLAGS   ?= $(ARCHFLAGS) -fPIC -ffreestanding -fno-strict-aliasing -fno-builtin \
            -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -O2 -Wall -Wextra
CFLAGS   += -I$(SDK)/sdk

LDFLAGS  ?= -shared -nostdlib -nodefaultlibs -nostartfiles \
            -Wl,-Bsymbolic -Wl,--no-undefined -Wl,--build-id=none \
            -Wl,-z,norelro -Wl,-z,max-page-size=0x1000 -Wl,--hash-style=sysv
LDFLAGS  += -Wl,-T,$(SDK)/sdk/prime_dyn.ld

LDLIBS   ?= -lgcc

# ---- 公共构件规则 ----
$(TARGET).elf: $(OBJS) $(SDK)/sdk/prime_dyn.ld
	$(ARMCC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

# 通用 C 编译规则（不能用 make 内置的 %.o: %.c —— 那会用宿主 cc）
%.o: %.c
	$(ARMCC) $(CFLAGS) -c -o $@ $<

# SDK 侧公共构件
prime_input.o: $(SDK)/sdk/prime_input.S
	$(ARMCC) $(CFLAGS) -c -o $@ $<

prime_hook.o: $(SDK)/sdk/prime_hook.c $(SDK)/sdk/prime_hook.h
	$(ARMCC) $(CFLAGS) -c -o $@ $<

# ---- 目标 ----
all: $(TARGET).elf

check: $(TARGET).elf
	$(READELF) -h -l -d -r $<

# deploy 仅在应用定义了 APPDIR 时生成（把 ELF 放进可上传包目录，
# 文件名必须是 my_app.elf —— 加载器两侧都按该名打开）。
ifdef APPDIR
deploy: $(TARGET).elf
	cp -f $(TARGET).elf $(APPDIR)/my_app.elf
	@echo "==> $(APPDIR)/ 已就绪（main.py + 元数据 + my_app.elf）"
.PHONY: deploy
endif

clean:
	rm -f $(OBJS) $(TARGET).elf $(EXTRA_CLEAN)

.PHONY: all check clean
