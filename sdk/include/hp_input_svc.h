/*
 * hp_input_svc.h -- shared input-hook state between the RESIDENT hook
 * service (compiled INTO tcc.elf, see hp_input_svc.c) and the code.elf-side
 * client (rt/hp_input.c + rt/hp_rt.c).
 *
 * The state lives in tcc.elf's .data: tcc.elf is loaded once and kept
 * resident, so the hook code and this state are NEVER freed.  main.py
 * scans tcc.elf for the "PRIMEIN" magic, computes the runtime address and
 * passes it to code.elf's entry in r0; hp_entry stores it in g_hp_input.
 * The OS-thread hook (in tcc.elf) and the app-thread client (in code.elf)
 * both manipulate this structure through that address -- the layout is
 * shared via this single header (same compiler on both sides).
 */
#ifndef HP_INPUT_SVC_H
#define HP_INPUT_SVC_H

#define HP_INPUT_MAGIC "PRIMEIN"
#define HP_Q_SIZE 64

struct hp_input_state {
    char magic[8];            /* "PRIMEIN" -- main.py finds the state by it */
    unsigned hook_fn;         /* absolute addr of the resident hook (reloc) */
    unsigned saved[4];        /* original slot bytes (16), restore on remove */
    unsigned tramp[4];        /* copy of the ORIGINAL slot bytes (16): the
                                 hook entry blx's here so the real get_event
                                 runs exactly as the firmware wrote it
                                 (gbemu/DOOM pattern) */
    int      saved_valid;     /* 1 once the true original was captured */
    volatile int head;        /* queue write index (hook / OS thread) */
    volatile int tail;        /* queue read index (app thread) */
    volatile int in_hook;     /* hook executing on the OS thread */
    volatile int hook_calls;  /* how many times the OS invoked the hook */
    volatile int drop_count;  /* events dropped while armed==0 (no program) */
    volatile int armed;       /* a program is listening (drop copies when 0) */
    struct hp_qitem {
        unsigned char type;   /* 1 = key, 2 = touch */
        unsigned char down;   /* key: 1 press, 0 release */
        unsigned char action; /* touch: firmware item type 1=press,
                               * 2=move, 8=release (moves not queued) */
        unsigned short key;   /* key: physical scan code */
        unsigned short x, y;  /* touch: screen coords */
    } q[HP_Q_SIZE];
};

#endif /* HP_INPUT_SVC_H */
