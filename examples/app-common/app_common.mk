# AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
#
# toolchain/examples/app-common/app_common.mk —— 应用公共运行支撑的构建片段。
#
# 用法（在应用的 Makefile 里，务必排在 toolchain/templates/app.mk 之后）：
#
#   include $(SDK)/templates/app.mk
#   include $(SDK)/examples/app-common/app_common.mk

APP_COMMON_DIR  ?= $(SDK)/examples/app-common
APP_COMMON_OBJS := app_common.o

OBJS        += $(APP_COMMON_OBJS)
EXTRA_CLEAN += $(APP_COMMON_OBJS)
CFLAGS      += -I$(APP_COMMON_DIR)

# 模板里 $(TARGET).elf 的依赖在此之前已展开，故显式追加一次
$(TARGET).elf: $(APP_COMMON_OBJS)

app_common.o: $(APP_COMMON_DIR)/app_common.c $(APP_COMMON_DIR)/app_common.h
	$(ARMCC) $(CFLAGS) -c -o $@ $(APP_COMMON_DIR)/app_common.c
