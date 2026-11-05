/* Inner loop of the bonus-level perspective floor.
 *
 * floor_row_<dir><m>: called by floor_rows (below) with the row's values in
 * a parameter block at a6 and the per-frame constants in registers.
 *
 * Fills one row of 40 tilemap words; each names the pair tile of two
 * horizontally adjacent floor samples: word = base | (left << 4) | right.
 * The C version (render_floor_c in bonus_view.c) is the reference; the two
 * must produce identical words (checked by the BONUS_SELFTEST build).
 *
 * The caller arranges both steps to be >= 0: rows whose u step is negative
 * are walked right to left ("rev", output written backwards), and rows whose
 * v step is still negative use a mirrored v ("m": texel row and tile row run
 * the other way).  Positions only keep the place inside the current tile:
 *
 *   U = 0xFFE00000 | texel column << 16 | fraction    (bits 20..16 = column)
 *   V = 0xFC000000 | texel row << 21    | fraction    (bits 25..21 = row)
 *
 * The guard ones above make the carry out of bit 31 fire exactly when a step
 * crosses into the next tile (a step is always less than one tile), so the
 * tile key and texel pointer only change then.
 *
 *   d0/d1 scratch  d2 tile key ^ 0x8000  d3 output word
 *   d4 U  d5 V  d6 dU  d7 dV
 *   a0 output  a1 grid + 0x8000  a2 tile pointer table  a4 tile texels
 *   a5 table of the same tiles with texels << 4   a3 current such tile
 *   (texel pointers +0x3E0 when mirrored, indexed with -row*32 + column)
 */
        .text
        .even

        /* texel index -> d1 */
        .macro  TEXEL mirror
        move.l  %d4,%d0
        swap    %d0
        and.w   #31,%d0
        move.l  %d5,%d1
        swap    %d1
        and.w   #0x3E0,%d1
        .if \mirror
        neg.w   %d1
        .endif
        or.w    %d0,%d1
        .endm

        .macro  LOOKUP mirror
        moveq   #0,%d0
        move.b  (%a1,%d2.w),%d0
        add.w   %d0,%d0
        add.w   %d0,%d0
        movea.l (%a2,%d0.w),%a4
        movea.l (%a5,%d0.w),%a3
        .if \mirror
        lea     0x3E0(%a4),%a4
        lea     0x3E0(%a3),%a3
        .endif
        .endm

        .macro  ADVANCE mirror
        add.l   %d6,%d4
        bcc.s   1f
        or.l    #0xFFE00000,%d4
        addq.b  #1,%d2
        LOOKUP  \mirror
1:
        add.l   %d7,%d5
        bcc.s   2f
        or.l    #0xFC000000,%d5
        .if \mirror
        sub.w   #0x100,%d2
        .else
        add.w   #0x100,%d2
        .endif
        LOOKUP  \mirror
2:
        .endm

        /* per-row values, in FloorRowParams order after the routine pointer */
        .macro  PROLOGUE
        movea.l (%a6)+,%a0      /* out */
        move.l  (%a6)+,%d4      /* U */
        move.l  (%a6)+,%d5      /* V */
        move.l  (%a6)+,%d6      /* dU */
        move.l  (%a6)+,%d7      /* dV */
        move.l  (%a6)+,%d2      /* tile key ^ 0x8000 */
        movea.l (%a6)+,%a4      /* texels of the first sample's tile */
        movea.l (%a6)+,%a3      /* the same tile, high-nibble copy */
        .endm

        .macro  EPILOGUE
        rts
        .endm

        /* left to right: left sample first */
        .macro  FORWARD mirror
        PROLOGUE
        .rept   40
        TEXEL   \mirror
        move.b  (%a3,%d1.w),%d3
        ADVANCE \mirror
        TEXEL   \mirror
        or.b    (%a4,%d1.w),%d3
        ADVANCE \mirror
        move.w  %d3,(%a0)+
        .endr
        EPILOGUE
        .endm

        /* right to left: right sample first, out points past the row */
        .macro  REVERSE mirror
        PROLOGUE
        lea     80(%a0),%a0
        .rept   40
        TEXEL   \mirror
        move.b  (%a4,%d1.w),%d3
        ADVANCE \mirror
        TEXEL   \mirror
        or.b    (%a3,%d1.w),%d3
        ADVANCE \mirror
        move.w  %d3,-(%a0)
        .endr
        EPILOGUE
        .endm

        .globl  floor_row_fwd
floor_row_fwd:
        FORWARD 0

        .globl  floor_row_fwd_m
floor_row_fwd_m:
        FORWARD 1

        .globl  floor_row_rev
floor_row_rev:
        REVERSE 0

        .globl  floor_row_rev_m
floor_row_rev_m:
        REVERSE 1

/* void floor_rows(const u32 *params, u32 rows, const u8 *grid,
 *                 const u8 *const *tbase, u32 word, const u8 *const *tbaseHi)
 *
 * Runs `rows` row routines.  params holds, per row: the routine (one of the
 * four above), out, U, V, dU, dV, key, texels, texelsHi.
 */
        .globl  floor_rows
floor_rows:
        movem.l %d2-%d7/%a2-%a6,-(%sp)
        movea.l 48(%sp),%a6     /* params */
        movea.l 56(%sp),%a1     /* grid + 0x8000 */
        movea.l 60(%sp),%a2     /* tbase */
        move.l  64(%sp),%d3     /* tilemap word of pair tile 0 */
        movea.l 68(%sp),%a5     /* tbaseHi */
        move.l  52(%sp),%d0
        subq.w  #1,%d0
        move.w  %d0,-(%sp)      /* rows left - 1 */
5:
        movea.l (%a6)+,%a0
        jsr     (%a0)
        subq.w  #1,(%sp)
        bpl.s   5b
        addq.l  #2,%sp
        movem.l (%sp)+,%d2-%d7/%a2-%a6
        rts

/* u32 bonus_scan(const u8 *events, u32 ty0, u32 tyStep, u32 col0,
 *                u32 colStep, u8 *hits)
 *
 * The 12 x 12 event cells around the bonus-level player, rows from ty0 and
 * columns from col0 in steps of +-1 (both wrap at 256).  Writes y << 4 | x
 * for every non-empty cell, row by row, and returns how many there were.
 * Nearly all cells are empty, so this is a byte test per cell.
 */
        .globl  bonus_scan
bonus_scan:
        movem.l %d2-%d7,-(%sp)
        movea.l 28(%sp),%a1     /* events */
        move.l  32(%sp),%d4     /* ty */
        move.l  36(%sp),%d5     /* ty step */
        move.l  40(%sp),%d0     /* first column */
        move.l  44(%sp),%d1     /* column step */
        movea.l 48(%sp),%a0     /* hits: kept in a0 while scanning */
        and.w   #255,%d0
        moveq   #0,%d3          /* y << 4 */
3:
        moveq   #0,%d2
        move.b  %d4,%d2
        lsl.w   #8,%d2
        adda.l  %d2,%a1         /* row start (undone below) */
        move.w  %d0,%d2         /* column; high byte stays 0 */
        moveq   #11,%d6
1:
        tst.b   (%a1,%d2.w)
        bne.s   4f
2:
        add.b   %d1,%d2
        dbra    %d6,1b
        moveq   #0,%d7
        move.b  %d4,%d7
        lsl.w   #8,%d7
        suba.l  %d7,%a1         /* back to the grid start */
        add.b   %d5,%d4
        add.b   #16,%d3
        cmp.b   #192,%d3
        bne.s   3b
        move.l  %a0,%d0
        sub.l   48(%sp),%d0
        movem.l (%sp)+,%d2-%d7
        rts
4:
        moveq   #11,%d7
        sub.b   %d6,%d7
        or.b    %d3,%d7
        move.b  %d7,(%a0)+
        bra.s   2b
