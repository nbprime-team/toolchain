# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
#
# sdk/unifont.mk —— unifont 运行库的公共构建片段（SDK 侧）。
#
# 用法（在应用 Makefile 里，排在 toolchain/templates/app.mk 之后）：
#
#   include $(SDK)/templates/app.mk
#   include $(SDK)/sdk/unifont.mk
#
# 提供：头文件包含路径、unifont_font.c/.h 生成规则、unifont_font.o /
#       unifont_draw.o 编译规则，并把二者并入 OBJS。

UNIFONT_DIR ?= $(SDK)/sdk/src/unifont
UNIFONT_HEX := $(UNIFONT_DIR)/unifont-17.0.04.hex
UNIFONT_C   := $(UNIFONT_DIR)/unifont_font.c
UNIFONT_H   := $(UNIFONT_DIR)/unifont_font.h

UNIFONT_OBJS := unifont_font.o unifont_draw.o
OBJS         += $(UNIFONT_OBJS)
EXTRA_CLEAN  += $(UNIFONT_OBJS)
CFLAGS       += -I$(SDK)/sdk/include -I$(UNIFONT_DIR)

$(TARGET).elf: $(UNIFONT_OBJS)

.PHONY: font fetch-unifont
font: $(UNIFONT_C) $(UNIFONT_H)
fetch-unifont: $(UNIFONT_HEX)

$(UNIFONT_HEX):
	$(PYTHON) $(UNIFONT_DIR)/fetch_unifont.py

$(UNIFONT_C): $(UNIFONT_HEX) $(UNIFONT_DIR)/fontgen.py
	$(PYTHON) $(UNIFONT_DIR)/fontgen.py $(UNIFONT_HEX) $(UNIFONT_C) $(UNIFONT_H)

$(UNIFONT_H): $(UNIFONT_C)

unifont_font.o: $(UNIFONT_C) $(UNIFONT_H)
	$(ARMCC) $(CFLAGS) -c -o $@ $(UNIFONT_C)

unifont_draw.o: $(UNIFONT_DIR)/unifont_draw.c $(SDK)/sdk/include/unifont_draw.h $(UNIFONT_H)
	$(ARMCC) $(CFLAGS) -c -o $@ $(UNIFONT_DIR)/unifont_draw.c
