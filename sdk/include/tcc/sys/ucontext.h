/* sys/ucontext.h —— HP Prime 移植的桩头文件：只提供 tccrun.c 的
 * rt_getcontext() 在 __arm__ 下读取的那些字段。 */
#ifndef _HP_SYS_UCONTEXT_H
#define _HP_SYS_UCONTEXT_H

typedef struct {
    unsigned long arm_r0, arm_r1, arm_r2, arm_r3;
    unsigned long arm_r4, arm_r5, arm_r6, arm_r7;
    unsigned long arm_r8, arm_r9, arm_r10, arm_fp;
    unsigned long arm_ip, arm_sp, arm_lr, arm_pc;
    unsigned long arm_cpsr;
    unsigned long fault_address;
} mcontext_t;

typedef struct ucontext {
    struct ucontext *uc_link;
    unsigned long uc_flags;
    void *uc_stack;
    mcontext_t uc_mcontext;
    void *uc_sigmask;
} ucontext_t;

#endif
