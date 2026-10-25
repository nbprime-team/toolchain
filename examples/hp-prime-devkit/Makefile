# HP Prime 原生程序交叉编译示例
# -------------------------------------------
# 目标产物必须满足 (详见 README 第 2 节):
#   - EM_ARM (0x28), PIC, vaddr 从 0 开始
#   - 无 libc 依赖 (freestanding)
#
# 用法:
#   make            # 编译 app.elf
#   make check      # readelf 校验 ELF 形态是否符合引导器约定
#   make clean

# ---------- 工具链 (按你本机安装选择) ----------
# G1 推荐: arm-none-eabi-gcc (Bare Metal)
# 也可用:  arm-linux-gnueabi-gcc
CC ?= arm-none-eabi-gcc
READELF ?= arm-none-eabi-readelf
OBJDUMP ?= arm-none-eabi-objdump

# ---------- CPU 目标 ----------
# G1 (ARM9, ARMv5TE):  -mcpu=arm926ej-s
# G2 (Cortex-A7, ARMv7-A): -mcpu=cortex-a7  (并可用 -marm 而非 -mthumb)
ARCH   ?= arm926ej-s
ARMSW  ?= -mthumb          # 或 -marm

TARGET  = app.elf
SRCS    = app_template.c

CFLAGS  = -Wall -O2 -ffreestanding -fno-builtin -fPIC \
          -mcpu=$(ARCH) $(ARMSW)
LDFLAGS = -nostdlib -Wl,-Ttext=0 -Wl,--no-dynamic-linker \
          -Wl,--build-id=none

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# 校验 ELF 是否符合引导器约定
check: $(TARGET)
	@echo "--- machine (应为 ARM 40) ---"
	$(READELF) -h $(TARGET) | grep -E "Machine|Type|Entry"
	@echo "--- LOAD 段 vaddr (首个应为 0x0) ---"
	$(READELF) -l $(TARGET) | grep LOAD
	@echo "--- 无动态重定位 (readelf -r 应为空/无 TEXTREL) ---"
	-$(READELF) -r $(TARGET)
	@echo "OK: 确认 Machine=ARM, 首 LOAD vaddr=0x0, Type=DYN/EXEC 均可"

disasm:
	$(OBJDUMP) -d $(TARGET) | head -80

clean:
	rm -f $(TARGET)
