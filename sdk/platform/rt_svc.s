/* rt_svc.s -- firmware SVC wrappers for TCC-compiled user programs.
 *
 * Same convention as hp_svc.s: push {r0}; push {lr}; svc N; the firmware
 * handler restores r0, runs f(r0,r1,r2,r3), returns through the pushed LR.
 * Built with arm-none-eabi-as into rt_svc.o, which TCC links into every
 * user program on the calculator.
 */
    .arm
    .syntax unified
    .text

    .globl hp_svc_fopen
hp_svc_fopen:
    push {r0}
    push {lr}
    svc 0x1026f

    .globl hp_svc_fclose
hp_svc_fclose:
    push {r0}
    push {lr}
    svc 0x100ca

    .globl hp_svc_fseek
hp_svc_fseek:
    push {r0}
    push {lr}
    svc 0x100cf

    .globl hp_svc_ftell
hp_svc_ftell:
    push {r0}
    push {lr}
    svc 0x100d0

    .globl hp_svc_fread
hp_svc_fread:
    push {r0}
    push {lr}
    svc 0x100d4

    .globl hp_svc_fwrite
hp_svc_fwrite:
    push {r0}
    push {lr}
    svc 0x100d7

    .globl hp_svc_filesize
hp_svc_filesize:
    push {r0}
    push {lr}
    svc 0x100cb

    .globl hp_svc_malloc
hp_svc_malloc:
    push {r0}
    push {lr}
    svc 0x10037

    .globl hp_svc_calloc
hp_svc_calloc:
    push {r0}
    push {lr}
    svc 0x10038

    .globl hp_svc_realloc
hp_svc_realloc:
    push {r0}
    push {lr}
    svc 0x10039

    .globl hp_svc_free
hp_svc_free:
    push {r0}
    push {lr}
    svc 0x1003a

    .globl hp_svc_sleep
hp_svc_sleep:
    push {r0}
    push {lr}
    svc 0x10008

    .globl hp_svc_get_event
hp_svc_get_event:
    push {r0}
    push {lr}
    svc 0x1003f

    .globl hp_svc_getlcd
hp_svc_getlcd:
    push {r0}
    push {lr}
    svc 0x1008d

    .globl hp_svc_create_thread
hp_svc_create_thread:
    push {r0}
    push {lr}
    svc 0x10000

    .globl hp_svc_terminate_thread
hp_svc_terminate_thread:
    push {r0}
    push {lr}
    svc 0x10001

    .end
