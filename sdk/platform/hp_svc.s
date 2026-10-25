/* hp_svc.s -- HP Prime G1 firmware SVC wrappers, setjmp/longjmp, crt0.
 *
 * Firmware syscall convention (verified on hardware by the DOOM/PureDOOM
 * and PrimeC projects):
 *     push {r0}
 *     push {lr}
 *     svc  N
 * The firmware SVC handler restores r0 from the stack, dispatches to the
 * OS routine f(r0,r1,r2,r3) -> r0, then returns through the pushed LR
 * (popping both words).  So each wrapper is exactly those three
 * instructions and the result arrives back in r0.
 */
    .arm
    .syntax unified
    .text

/* ---------------- firmware SVC wrappers ---------------- */

    .globl hp_svc_fopen
hp_svc_fopen:                       /* svc 0x1026f: fopen(utf16path, utf16mode) */
    push {r0}
    push {lr}
    svc 0x1026f

    .globl hp_svc_fclose
hp_svc_fclose:                      /* svc 0x100ca: fclose(fd) */
    push {r0}
    push {lr}
    svc 0x100ca

    .globl hp_svc_fseek
hp_svc_fseek:                       /* svc 0x100cf: fseek(fd, offset, whence) */
    push {r0}
    push {lr}
    svc 0x100cf

    .globl hp_svc_ftell
hp_svc_ftell:                       /* svc 0x100d0: ftell(fd) */
    push {r0}
    push {lr}
    svc 0x100d0

    .globl hp_svc_fread
hp_svc_fread:                       /* svc 0x100d4: fread(buf, nmemb, size, fd) */
    push {r0}
    push {lr}
    svc 0x100d4

    .globl hp_svc_fwrite
hp_svc_fwrite:                      /* svc 0x100d7: fwrite(buf, nmemb, size, fd) */
    push {r0}
    push {lr}
    svc 0x100d7

    .globl hp_svc_filesize
hp_svc_filesize:                    /* svc 0x100cb: filesize(fd) */
    push {r0}
    push {lr}
    svc 0x100cb

    .globl hp_svc_malloc
hp_svc_malloc:                      /* svc 0x10037: malloc(n) */
    push {r0}
    push {lr}
    svc 0x10037

    .globl hp_svc_calloc
hp_svc_calloc:                      /* svc 0x10038: calloc(n, sz) */
    push {r0}
    push {lr}
    svc 0x10038

    .globl hp_svc_realloc
hp_svc_realloc:                     /* svc 0x10039: realloc(p, n) */
    push {r0}
    push {lr}
    svc 0x10039

    .globl hp_svc_free
hp_svc_free:                        /* svc 0x1003a: free(p) */
    push {r0}
    push {lr}
    svc 0x1003a

    .globl hp_svc_sleep
hp_svc_sleep:                       /* svc 0x10008: os_sleep(ms) */
    push {r0}
    push {lr}
    svc 0x10008

    .globl hp_svc_getlcd
hp_svc_getlcd:                      /* svc 0x1008d: get_lcd(...) */
    push {r0}
    push {lr}
    svc 0x1008d

    .globl hp_svc_get_event
hp_svc_get_event:                   /* svc 0x1003f: get_event(ev) (blocking) */
    push {r0}
    push {lr}
    svc 0x1003f

    .globl hp_svc_create_thread
hp_svc_create_thread:               /* svc 0x10000: create_thread(fn, arg, stack) */
    push {r0}
    push {lr}
    svc 0x10000

    .globl hp_svc_terminate_thread
hp_svc_terminate_thread:            /* svc 0x10001: terminate_thread(...) */
    push {r0}
    push {lr}
    svc 0x10001

/* ---------------- setjmp / longjmp (AAPCS soft-float) ---------------- */

    .globl setjmp
setjmp:                             /* r0 = jmp_buf (40 bytes) */
    stmia r0!, {r4, r5, r6, r7, r8, r9, r10, r11}
    str   sp, [r0], #4
    str   lr, [r0]
    mov   r0, #0
    bx    lr

    .globl longjmp
longjmp:                            /* r0 = jmp_buf, r1 = val */
    ldmia r0!, {r4, r5, r6, r7, r8, r9, r10, r11}
    ldr   sp, [r0], #4
    ldr   lr, [r0]
    cmp   r1, #0
    moveq r0, #1
    movne r0, r1
    bx    lr

/* ---------------- crt0 ----------------
 * The shellcode loader calls the ELF entry with:
 *     r0 = config pointer (struct hp_config), r1 = 0
 * We run on the caller's stack (gbemu/DOOM style - no stack switch), save
 * the config, then run main(argc, argv) via hp_c_main().  exit(n)
 * longjmps back here with r0 = n.
 */
    .globl _start
_start:
    ldr   r1, =hp_retaddr
    str   lr, [r1]                  /* save the loader's return address */

    ldr   r1, =hp_config_ptr
    str   r0, [r1]                  /* stash the config pointer */

    ldr   r0, =stage_s1
    bl    hp_stage_marker           /* S1: entry reached */

    ldr   r0, =hp_exit_env
    bl    setjmp
    cmp   r0, #0
    bne   1f                        /* longjmp back: r0 == exit code */

    bl    hp_c_main                 /* runs main(argc, argv); returns code */
    b     2f
1:
2:
    ldr   r1, =hp_exit_code
    str   r0, [r1]                  /* record exit code (also in r0) */
    ldr   r1, =hp_retaddr
    ldr   r1, [r1]
    bx    r1                        /* return to the loader with r0 = code */

/* ---------------- data ---------------- */
    .data
stage_s1:
    .asciz "S1"
    .balign 4
    .globl hp_retaddr
hp_retaddr:     .word 0
    .globl hp_exit_code
hp_exit_code:   .word 0
    .globl hp_config_ptr
hp_config_ptr:  .word 0
    .globl hp_exit_env
hp_exit_env:    .space 40

/* 128 KB stack in .bss (currently unused: we run on the caller's stack) */
    .bss
    .balign 8
__hp_stack:
    .space 131072
__hp_stack_top:
    .end
