/*
 * X6886 probe: position-independent, no font, no absolute data addresses.
 * LK boots this as a Linux arm64 Image with MMU and caches off.
 * It paints raw bars on LK's framebuffer, writes hand-off state into the
 * ramoops console zone in the exact format Linux's pstore driver reads,
 * then chains to Android's kernel packed after it.
 *
 * Register contract:
 *   x19 = real load address (from ADR, PC-relative)
 *   x20 = LK's device tree
 *   x22 = ramoops zone A data start (to compute the count at the end)
 *   x23/x24 = zone A / zone B write cursors
 *   puts reads its string from x1; hex64 reads its value from x0.
 */
.global _start
.section .text.boot

_start:
    b       real_start
    .word   0
    .quad   0                   /* text_offset */
    .quad   0                   /* image_size, patched by chainboot */
    .quad   0xa                 /* flags */
    .quad   0
    .quad   0                   /* res2: Android Image length, patched */
    .quad   0                   /* res3: payload offset, patched */
    .ascii  "ARM\x64"
    .word   0

real_start:
    mov     x20, x0             /* LK's device tree */
    adr     x19, _start         /* our real load address */

    /*
     * Two candidate pstore console zones. Write the same log to both, then Android tells us
     * which one its driver actually reads as /sys/fs/pstore/console-ramoops-0.
     *   A = 0x48090000 (ramoops region base, from the X6886 cmdline)
     *   B = 0x4811f000 (console zone used by the working MTK reference)
     * x23/x24 keep the two data cursors; x22 keeps the start for the count.
     */
    movz    x1, #0x0000
    movk    x1, #0x4809, lsl #32
    add     x23, x1, #12
    movz    x1, #0xf000
    movk    x1, #0x11, lsl #16
    movk    x1, #0x4811, lsl #32
    add     x24, x1, #12
    mov     x22, x23                /* zone A data start, for the final count */

    movz    x0, #0x4244
    movk    x0, #0x4347, lsl #16    /* 0x43474244 "DBGC" */
    str     w0, [x23, #-12]
    str     w0, [x24, #-12]
    mov     w0, wzr
    str     w0, [x23, #-8]
    str     w0, [x23, #-4]
    str     w0, [x24, #-8]
    str     w0, [x24, #-4]

    /* ---- framebuffer bars: 4 bands of 60 rows, raw pixels, no font ---- */
    movz    x0, #0x0000
    movk    x0, #0xfe0c, lsl #32    /* LK framebuffer 0xfe0c0000 */
    mov     x2, #7680
    mov     x3, #120
    mul     x2, x2, x3
    add     x0, x0, x2
    movz    x1, #0x0000
    movk    x1, #0xffff, lsl #16    /* red */
    movz    x2, #0xc200
    movk    x2, #0x1, lsl #16       /* 7680*60/4 = 0x1c200 words */
    bl      fill

    movz    x0, #0x0000
    movk    x0, #0xfe0c, lsl #32
    mov     x2, #7680
    mov     x3, #180
    mul     x2, x2, x3
    add     x0, x0, x2
    movz    x1, #0xff00
    movk    x1, #0xff00, lsl #16    /* green */
    movz    x2, #0xc200
    movk    x2, #0x1, lsl #16
    bl      fill

    movz    x0, #0x0000
    movk    x0, #0xfe0c, lsl #32
    mov     x2, #7680
    mov     x3, #240
    mul     x2, x2, x3
    add     x0, x0, x2
    movz    x1, #0x0000
    movk    x1, #0x00ff, lsl #16    /* blue */
    movz    x2, #0xc200
    movk    x2, #0x1, lsl #16
    bl      fill

    movz    x0, #0x0000
    movk    x0, #0xfe0c, lsl #32
    mov     x2, #7680
    mov     x3, #300
    mul     x2, x2, x3
    add     x0, x0, x2
    movz    x1, #0xffff
    movk    x1, #0xffff, lsl #16    /* white */
    movz    x2, #0xc200
    movk    x2, #0x1, lsl #16
    bl      fill

    /* ---- log: puts(string in x1), hex64(value in x0) ---- */
    adr     x1, s_magic
    bl      puts

    adr     x1, s_load
    bl      puts
    mov     x0, x19
    bl      hex64
    bl      newline

    adr     x1, s_dtb
    bl      puts
    mov     x0, x20
    bl      hex64
    bl      newline

    mrs     x0, CurrentEL
    adr     x1, s_el
    bl      puts
    lsr     x0, x0, #2
    bl      hex64
    bl      newline

    adr     x1, s_freq
    bl      puts
    mrs     x0, CNTFRQ_EL0
    bl      hex64
    bl      newline

    adr     x1, s_midr
    bl      puts
    mrs     x0, MIDR_EL1
    bl      hex64
    bl      newline

    adr     x1, s_mpidr
    bl      puts
    mrs     x0, MPIDR_EL1
    bl      hex64
    bl      newline

    adr     x1, s_sp
    bl      puts
    mov     x0, sp
    bl      hex64
    bl      newline

    /* SCTLR of the current EL */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.ne    1f
    mrs     x0, sctlr_el2
    adr     x1, s_sctlr
    bl      puts
    bl      hex64
    bl      newline
    b       2f
1:  mrs     x0, sctlr_el1
    adr     x1, s_sctlr
    bl      puts
    bl      hex64
    bl      newline
2:

    /* OVL0 candidates: real 0x14005000 vs the copied 0x1400b000 */
    movz    x1, #0x5000
    movk    x1, #0x1400, lsl #16
    ldr     w0, [x1, #0xf40]
    mov     x5, x0
    adr     x1, s_ovl_a
    bl      puts
    mov     x0, x5
    bl      hex64
    bl      newline

    movz    x1, #0xb000
    movk    x1, #0x1400, lsl #16
    ldr     w0, [x1, #0xf40]
    mov     x5, x0
    adr     x1, s_ovl_b
    bl      puts
    mov     x0, x5
    bl      hex64
    bl      newline

    /* TOPRGU watchdog length */
    movz    x1, #0x7000
    movk    x1, #0x1000, lsl #16
    ldr     w0, [x1, #4]
    mov     x5, x0
    adr     x1, s_wdt
    bl      puts
    mov     x0, x5
    bl      hex64
    bl      newline

    /* marker words the full loader reads before choosing Android vs XNU */
    movz    x1, #0x0000
    movk    x1, #0x8000, lsl #32
    ldr     w0, [x1]
    mov     x5, x0
    adr     x1, s_mark0
    bl      puts
    mov     x0, x5
    bl      hex64
    bl      newline
    movz    x1, #0x0000
    movk    x1, #0x8000, lsl #32
    ldr     w0, [x1, #0x40]
    mov     x5, x0
    adr     x1, s_mark1
    bl      puts
    mov     x0, x5
    bl      hex64
    bl      newline

    adr     x1, s_done
    bl      puts

    /* pstore reads data at (start - size); with data at 0..N-1 set both to N */
    sub     x0, x23, x22            /* N = end - start, positive */
    movz    x1, #0x0000
    movk    x1, #0x4809, lsl #32
    str     w0, [x1, #4]            /* zone A start = N */
    str     w0, [x1, #8]            /* zone A size = N */
    movz    x1, #0xf000
    movk    x1, #0x11, lsl #16
    movk    x1, #0x4811, lsl #32
    str     w0, [x1, #4]            /* zone B start = N */
    str     w0, [x1, #8]            /* zone B size = N */
    dsb     sy

    /* ---- chain back to Android ---- */
    ldr     x4, [x19, #0x28]        /* res2 = Android Image length */
    cbz     x4, hang
    mov     x1, x19                 /* dst = our load address */
    mov     x5, #0x40000
    add     x2, x19, x5             /* src = load + 0x40000 */

    /* relocate the copy stub to 0x480a0000 (inside ramoops, 64KB from our log).
     * Use x6 as the fixed base and x11 as the moving pointer, so br x6 is the stub entry. */
    movz    x6, #0x0000
    movk    x6, #0x480a, lsl #32
    mov     x11, x6
    adr     x7, chain_stub
    adr     x8, chain_stub_end
    sub     x9, x8, x7
stub_copy:
    ldr     x10, [x7], #8
    str     x10, [x11], #8
    subs    x9, x9, #8
    b.ne    stub_copy
    dsb     sy
    ic      iallu
    dsb     sy
    isb

    /* chain_stub(dtb=x0, dst=x1, src=x2, len=x3, entry=x4) */
    mov     x0, x20
    mov     x3, x4                  /* length */
    mov     x4, x19                 /* entry = Android Image at load address */
    br      x6                      /* x6 still = 0x480a0000, the stub's first instruction */

hang:
    wfe
    b       hang

/* fill: x0 = address, x1 = colour, x2 = word count */
fill:
    str     w1, [x0], #4
    subs    x2, x2, #1
    b.ne    fill
    ret

/* puts: x1 = string address, preserves x0. Writes bytes to both cursors x23/x24. */
puts:
    ldrb    w2, [x1], #1
    cbz     w2, 2f
    strb    w2, [x23], #1
    strb    w2, [x24], #1
    b       puts
2:  ret

newline:
    mov     w2, #10
    strb    w2, [x23], #1
    strb    w2, [x24], #1
    mov     w2, #0
    strb    w2, [x23], #1
    strb    w2, [x24], #1
    ret

/* hex64: x0 = value, writes 16 hex chars to both cursors. Clobbers x2,x3,x4. */
hex64:
    mov     x3, #16
1:  and     x2, x0, #0xf
    adr     x4, hexchars
    ldrb    w2, [x4, x2]
    strb    w2, [x23], #1
    strb    w2, [x24], #1
    lsr     x0, x0, #4
    subs    x3, x3, #1
    b.ne    1b
    ret

chain_stub:
1:  ldr     x10, [x2], #8
    str     x10, [x1], #8
    subs    x3, x3, #8
    b.hi    1b
    dsb     sy
    ic      iallu
    dsb     sy
    isb
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    br      x4
chain_stub_end:

hexchars:   .ascii "0123456789abcdef"
s_magic:    .asciz "\n== X6886 PROBE ==\n"
s_load:     .asciz "LOAD="
s_dtb:      .asciz "DTB="
s_el:       .asciz "EL="
s_freq:     .asciz "CNTFRQ="
s_midr:     .asciz "MIDR="
s_mpidr:    .asciz "MPIDR="
s_sp:       .asciz "SP="
s_sctlr:    .asciz "SCTLR="
s_ovl_a:    .asciz "OVL0_14005000="
s_ovl_b:    .asciz "OVL0_1400b000="
s_wdt:      .asciz "WDT_LEN="
s_mark0:    .asciz "MARK0="
s_mark1:    .asciz "MARK1="
s_done:     .asciz "PROBE_DONE\n"
