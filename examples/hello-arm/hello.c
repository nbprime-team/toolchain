/*
 * hello-arm —— 接口性示例（工具链最小闭环）
 *
 * ⚠️ AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）
 *
 * 验证：交叉编译 → 汇编 → 链接（newlib + nosys）→ ELF 架构正确（ARM / ARMv5TEJ）。
 * 只做接口验证，不产出可上机包（工程性示例见 app-collection/）。
 */
#include <stdio.h>

int main(void)
{
    printf("hello from %s\n", "ARM");
    return 0;
}
