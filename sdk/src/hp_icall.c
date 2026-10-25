/*
 * hp_icall.c -- GCC->TCC 间接调用 thunk（见 hp_icall.h）。
 *
 * TCC 编译的代码会撕碎 r4-r11；从这个 GCC runtime 进入 app 代码的
 * 每次间接调用都必须经过这里，这样调用者的 state 才能存活。
 * Naked asm：save/restore pair 精确地括住 blx。
 */
#include "hp_icall.h"

__attribute__((naked)) int hp_icall2(void *fn, unsigned a, unsigned b)
{
    __asm__ __volatile__(
        "stmfd sp!, {r4, r5, r6, r7, r8, r9, r10, r11, ip, lr}\n\t"
        "mov ip, r0\n\t"               /* ip = fn */
        "mov r0, r1\n\t"               /* r0 = a */
        "mov r1, r2\n\t"               /* r1 = b */
        "blx ip\n\t"                   /* fn(a, b)；r4-r11 可能被吃掉 */
        "ldmfd sp!, {r4, r5, r6, r7, r8, r9, r10, r11, ip, lr}\n\t"
        "bx lr\n\t");                  /* r0 = fn 的返回值 */
}
