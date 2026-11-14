/* Genesis front end for the JJ1 bonus levels.
 *
 * The floor is the original's perspective texture map, sampled at 80x20 with
 * each sample a 4x4 pixel block.  Two horizontally adjacent samples make the
 * top half of one of 256 preloaded "pair" tiles (colour a | colour b, bottom
 * half transparent), so the floor is drawn as tilemaps only: plane B carries
 * the even sample rows, plane A - scrolled 4 lines down - the odd ones, each
 * row's opaque top half covering the other plane's transparent bottom half.
 * Per frame that is 800 map words instead of 12.8 KB of tile pixels.  Items are the original gem/clock/glove/exit
 * graphics, pre-scaled to four sprite sizes. The player is the original
 * 72x88 bonus animation, streamed as a nine-sprite VDP composite. */
#include <genesis.h>
#include "jj1_options.h"
#include "jazz_gfx.h"
#include "bonus_view.h"
#include "jj1_bonus.h"
#include "jj1_pack.h"
#include "jj1_sounds.h"
#include "jj1_bonus_data.inc"
#include "jj1_bonus_player.inc"

#define FLOOR_COLS   40
#define FLOOR_ROWS   10
#define FLOOR_TILES  (FLOOR_COLS * FLOOR_ROWS)
#define SAMPLES_X    80             /* each sample is a 4x4 pixel block */
#define SAMPLES_Y    20
#define FLOOR_TOP_ROW 18            /* tile row where the floor starts (y = 144) */

#define T_FLOOR   T_JJ1_BLOCK_CACHE
#define T_PAIR    1024              /* 256 pair tiles; low byte of the index is 0 */
#define T_STRIP   T_FLOOR
#define T_ITEMS   (T_STRIP + 192)
#define ITEM_TILES 30               /* 16 + 9 + 4 + 1 per item type */
#define T_HERO    (T_ITEMS + 4 * ITEM_TILES)
#define HERO_TILES 99
#define T_SKY     (T_HERO + HERO_TILES)    /* 15 solid tiles, one per gradient row */

typedef char bonus_vram_fits[(T_SKY + 15 <= T_PAIR) ? 1 : -1];
typedef char bonus_pair_fits[(T_PAIR + 256 <= JJ1_VRAM_TILE_LIMIT) ? 1 : -1];
#define PAIR_WORD TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE, T_PAIR)

#define SFX_BONUS_GOD 77            /* XGM PCM ids; the main game uses 64-76 */
#define SFX_BONUS_OW  68            /* shared with the main game's "ow" */

#define MUL(x, y) ((s32)(((s32)(x) * (s32)(y)) >> 10))

/* MUL(x, c) for a sine/cosine c (|c| <= 1024) with two 16x16 hardware
 * multiplies instead of libgcc's 32x32 routine.  x = hi * 32768 + lo, so
 * x * c = ((hi * c) << 15) + lo * c, which wraps exactly like the 32-bit
 * product as long as hi fits 16 bits (|x| < 2^30). */
/* 16 x 16 -> 32 signed multiply.  GCC calls libgcc's __mulsi3 for these
 * when the operands have been widened to int; muls.w is several times
 * faster. */
static inline s32 muls16(s16 a, s16 b)
{
    s32 r = a;
    __asm__("muls.w %1,%0" : "+d"(r) : "d"(b));
    return r;
}

static inline s32 mulc(s32 x, s16 c)
{
    s16 hi = (s16)(x >> 15), lo = (s16)(x & 32767);
    return (s32)((muls16(hi, c) << 15) + muls16(lo, c)) >> 10;
}

static inline u32 mulu16(u16 a, u16 b)
{
    u32 r = a;
    __asm__("mulu.w %1,%0" : "+d"(r) : "d"(b));
    return r;
}

static inline u32 divuw(u32 a, u16 d)
{
    __asm__("divu.w %1,%0" : "+d"(a) : "d"(d));
    return a & 0xFFFF;
}

/* n / d (truncating like C) for d > 0 and |n| < 768 d, i.e. a quotient
 * that fits 10 bits.  A divisor over 16 bits is shifted down and rounded up,
 * which can only make the estimate one too small; one multiply fixes it. */
static inline s32 div_small(s32 n, s32 d)
{
    u32 a = (u32)((n < 0) ? -n : n), q;
    if (d < 65536) q = divuw(a, (u16)d);
    else {
        u32 dd = (u32)d;
        u8 sh = 0;
        while (dd >= 32768) { dd >>= 1; sh++; }
        q = divuw(a >> sh, (u16)(dd + 1));
        {
            u16 q1 = (u16)(q + 1);
            u32 p = mulu16(q1, (u16)((u32)d & 0xFFFF)) + (mulu16(q1, (u16)((u32)d >> 16)) << 16);
            if (p <= a) q++;
        }
    }
    return (n < 0) ? -(s32)q : (s32)q;
}

/* x / 80 for |x / 80| < 32768, truncating like C, with one divs. */
static inline s32 div80(s32 x)
{
    s32 q = x;
    __asm__("divs.w #80,%0" : "+d"(q));
    return (s16)q;
}

/* Tilemap rows of the floor: [0] even sample rows (plane B), [1] odd (plane A).
 * Rows are as wide as the 64-cell planes, so each plane's floor is one DMA. */
#define PLANE_W 64
static u16 floorMap[2][FLOOR_ROWS][PLANE_W];
static const u8 *tbase[60];         /* texels of each floor tile */
static const u8 *tbaseHi[60];       /* the same, texels << 4 */
static s32 rowDist[SAMPLES_Y];
static BonusGame bg;

/* Nine hardware sprites preserve the original 68x88 art at native size. */
static u16 draw_player(u16 count, s16 zpx)
{
    static const u8 widths[3] = {4, 4, 1};
    static const u8 heights[3] = {4, 4, 3};
    u16 tile = T_HERO;
    u8 row, col;
    for (row = 0; row < 3; row++) {
        for (col = 0; col < 3; col++) {
            VDP_setSpriteFull(count, 124 + col * 32, 120 - zpx + row * 32,
                SPRITE_SIZE(widths[col], heights[row]),
                TILE_ATTR_FULL(PAL3, TRUE, FALSE, FALSE, tile), (u8)(count + 1));
            count++;
            tile += widths[col] * heights[row];
        }
    }
    return count;
}

/* One row of FLOOR_COLS map words (src/floor_asm.s).  The four variants
 * cover the step directions; see the comment there. */
extern void floor_row_fwd(void), floor_row_fwd_m(void), floor_row_rev(void), floor_row_rev_m(void);
extern void floor_rows(const u32 *params, u32 rows, const u8 *grid, const u8 *const *tbase,
                       u32 word, const u8 *const *tbaseHi);

/* Row set-up shared by both renderers: start position and step in 1/1024 px. */
#define ROW_SETUP()                                                              \
    s32 d = rowDist[sy];                                                         \
    s32 sideX = mulc(d, cs), sideY = mulc(d, sn);                                \
    /* MUL(d - 16384, c) = MUL(d, c) - 16 c exactly: 16384 c is a multiple   \
       of 1024. */                                                               \
    s32 ur = b->x + (sideY - 16 * sn) - (sideX >> 1);                            \
    s32 vr = b->y - (sideX - 16 * cs) - (sideY >> 1);                            \
    s32 dur = div80(sideX), dvr = div80(sideY);                                  \
    u16 *o = floorMap[sy & 1][sy >> 1]

/* The reference: every sample looks its tile up from scratch.  Pixel = raw
 * position >> 10, tile = pixel >> 5, both wrapping at 256 tiles. */
static void __attribute__((unused)) render_floor_c(const BonusGame *b)
{
    const s16 sn = (s16)bonus_sin(b->direction), cs = (s16)bonus_cos(b->direction);
    const u8 *grid = b->grid;
    u8 sy;
    for (sy = 0; sy < SAMPLES_Y; sy++) {
        ROW_SETUP();
        u8 n, k;
        for (n = 0; n < FLOOR_COLS; n++) {
            u16 w = 0;
            for (k = 0; k < 2; k++) {
                u16 pu = (u16)(ur >> 10), pv = (u16)(vr >> 10);
                const u8 *tp = tbase[grid[((pv << 3) & 0xFF00) | ((pu >> 5) & 0xFF)]];
                w = (u16)((w << 4) | tp[((pv & 31) << 5) | (pu & 31)]);
                ur += dur; vr += dvr;
            }
            *o++ = (u16)(PAIR_WORD | (w & 0xFF));
        }
    }
}

static void __attribute__((unused)) render_floor(const BonusGame *b)
{
    /* per row: routine, out, U, V, dU, dV, key, texels, texelsHi */
    static u32 params[SAMPLES_Y][9];
    const s16 sn = (s16)bonus_sin(b->direction), cs = (s16)bonus_cos(b->direction);
    const u8 *grid = b->grid;
    u8 sy;
    for (sy = 0; sy < SAMPLES_Y; sy++) {
        ROW_SETUP();
        void (*fn)(void);
        u8 mirror;
        u16 key;
        const u8 *tp, *tph;
        u32 *pr = params[sy];
        if (dur < 0) {          /* walk the row from its right end */
            ur += 79 * dur; vr += 79 * dvr; dur = -dur; dvr = -dvr;
            mirror = (dvr < 0);
            fn = mirror ? floor_row_rev_m : floor_row_rev;
        } else {
            mirror = (dvr < 0);
            fn = mirror ? floor_row_fwd_m : floor_row_fwd;
        }
        key = (u16)((((u32)vr >> 7) & 0xFF00) | (((u32)ur >> 15) & 0xFF));
        tp = tbase[grid[key]];
        tph = tbaseHi[grid[key]];
        if (mirror) {
            /* v' = (2^23 - 1) - v: pixel' = 8191 - pixel, so the texel row
               and the tile row both count down while v' counts up. */
            vr = 0x7FFFFF - (vr & 0x7FFFFF);
            dvr = -dvr;
            tp += 0x3E0;
            tph += 0x3E0;
        }
        pr[0] = (u32)fn;
        pr[1] = (u32)o;
        pr[2] = 0xFFE00000UL | ((u32)(ur & 0x7FFF) << 6);
        pr[3] = 0xFC000000UL | ((u32)(vr & 0x7FFF) << 11);
        pr[4] = (u32)dur << 6;
        pr[5] = (u32)dvr << 11;
        pr[6] = (u16)(key ^ 0x8000);
        pr[7] = (u32)tp;
        pr[8] = (u32)tph;
    }
    floor_rows(params[0], SAMPLES_Y, grid + 0x8000, tbase, PAIR_WORD, tbaseHi);
}

/* Planes for the floor: B keeps hscroll 0 below the strip, A is shifted 4
 * lines down so its rows interleave with B's. */
static void floor_upload(void)
{
    DMA_queueDma(DMA_VRAM, floorMap[0][0], VDP_getPlaneAddress(BG_B, 0, FLOOR_TOP_ROW),
                 FLOOR_ROWS * PLANE_W, 2);
    DMA_queueDma(DMA_VRAM, floorMap[1][0], VDP_getPlaneAddress(BG_A, 0, FLOOR_TOP_ROW),
                 FLOOR_ROWS * PLANE_W, 2);
}

static void load_pair_tiles(void)
{
    static u32 pair[16][8];         /* one second-colour group at a time */
    u8 a, b2, y;
    for (a = 0; a < 16; a++) {
        for (b2 = 0; b2 < 16; b2++) {
            u32 row = ((u32)a * 0x11110000UL) | ((u32)b2 * 0x1111UL);
            for (y = 0; y < 4; y++) pair[b2][y] = row;
            for (y = 4; y < 8; y++) pair[b2][y] = 0;
        }
        VDP_loadTileData(pair[0], T_PAIR + a * 16, 16, CPU);
    }
}

typedef struct { s16 x; u8 ev; u8 size; s32 div; } Seen;
extern u32 bonus_scan(const u8 *events, u32 ty0, u32 tyStep, u32 col0, u32 colStep, u8 *hits);
#ifdef BONUS_PROF
u16 profItems;
u32 profScan;
#endif

static u16 __attribute__((noinline)) draw_items(const BonusGame *b, u16 count)
{
    Seen seen[40];
    u8 n = 0, i, j;
    const s16 sn = (s16)bonus_sin(b->direction), cs = (s16)bonus_cos(b->direction);
    s32 px = b->x, py = b->y;
    /* With sX = (evX << 15) + cx, MUL(sX, c) = evX * c * 32 + MUL(cx, c)
       exactly (the first product is a multiple of 1024), so the four
       products per cell become a per-frame constant plus a small multiply. */
    const s32 cx = 16384 - (px & 32767), cy = 16384 - (py & 32767);
    const s32 axs = mulc(cx, sn), axc = mulc(cx, cs), ays = mulc(cy, sn), ayc = mulc(cy, cs);
    const u8 flipY = ((b->direction - 256) & 512) != 0, flipX = (b->direction & 512) != 0;
    const int baseX = (int)(px >> 15), baseY = (int)(py >> 15);
    const u8 bx = (u8)baseX;
    /* Columns run from evX = x0 in steps of colStep, rows likewise. */
    const s16 x0 = flipX ? -6 : 6, y0 = flipY ? -6 : 6;
    const s8 colStep = flipX ? 1 : -1, rowStep = flipY ? 1 : -1;
    u8 hits[144], nh, h;
    /* Nearly every cell is empty: an assembly scan (src/floor_asm.s) lists
       the others, row by row, and only those are projected. */
    nh = (u8)bonus_scan(b->events, (u8)(y0 + baseY), (u32)(s32)rowStep,
                        (u8)(bx + x0), (u32)(s32)colStep, hits);
    for (h = 0; h < nh; h++) {
        {
            const u8 yk = hits[h] >> 4, k = hits[h] & 15;
            const s16 evY = (s16)(y0 + (flipY ? yk : -yk));
            const s16 evX = (s16)(x0 + (flipX ? k : -k));
            const u8 ty = (u8)(evY + baseY), col = (u8)(evX + bx);
            const u8 ev = b->events[((u16)ty << 8) | col];
            s32 div, num, an;
            div = 16384 + ((muls16(evX, sn) << 5) + axs) - ((muls16(evY, cs) << 5) + ayc);
            /* div >> 10 <= 8  <=>  div < 9216 */
            if (div < 9216) continue;
            num = ((muls16(evX, cs) << 5) + axc) + ((muls16(evY, sn) << 5) + ays);
            /* |num << 10| >= 768 div  <=>  4 |num| >= 3 div; then |nX| >= 768,
               which lands at least 80 px off screen either way. */
            an = (num < 0) ? -num : num;
            if ((an << 2) >= div + div + div) continue;
            if ((b->evType[ev & 15] <= 0) || (b->evType[ev & 15] == 5) || (n >= 40)) continue;
            if (!bonus_event(b, col, ty)) continue;
            {
                s32 nX = div_small(num << 10, div);
                s32 scr = (muls16((s16)nX, 320) >> 10) + 160;   /* |nX| < 768 */
                /* Size class of pxsize = (24 * ((65536 << 10) / div)) >> 10,
                   decided by comparing div with the exact break points
                   (pxsize >= T  <=>  div <= 2^26 / ceil(1024 T / 24)). */
#define DIV_FOR(T) (67108864L / ((1024L * (T) + 23) / 24))
                u8 size = (div <= DIV_FOR(28)) ? 0 : (div <= DIV_FOR(20)) ? 1 :
                          (div <= DIV_FOR(12)) ? 2 : 3;
                if (scr > -40 && scr < 360) {
                    seen[n].x = (s16)scr; seen[n].ev = ev; seen[n].size = size; seen[n].div = div; n++;
                }
            }
        }
    }
#ifdef BONUS_PROF
    { extern u16 profItems; extern u32 profScan; profItems = n; profScan = getSubTick(); }
#endif
    /* nearest first, so near items are drawn on top */
    for (i = 1; i < n; i++) {
        Seen t = seen[i];
        for (j = i; j > 0 && seen[j - 1].div > t.div; j--) seen[j] = seen[j - 1];
        seen[j] = t;
    }
    for (i = 0; i < n && count < 70; i++) {
        static const u8 px_size[4] = { 32, 24, 16, 8 };
        static const u8 tiles_n[4] = { 16, 9, 4, 1 };
        static const u8 vdp_size[4] = { SPRITE_SIZE(4, 4), SPRITE_SIZE(3, 3), SPRITE_SIZE(2, 2), SPRITE_SIZE(1, 1) };
        u8 s = seen[i].size, off = 0, k, kind = b->evType[seen[i].ev & 15];
        u8 item = (kind == 1) ? 0 : (kind == 2) ? 1 : (kind == 8) ? 2 : 3;
        for (k = 0; k < s; k++) off += tiles_n[k];
        VDP_setSpriteFull(count, seen[i].x - (px_size[s] >> 1), 129 - (px_size[s] >> 1),
                          vdp_size[s], TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE, T_ITEMS + item * ITEM_TILES + off),
                          (u8)(count + 1));
        count++;
    }
    return count;
}

static u16 hudShownGems, hudShownItems, hudShownTime = 0xFFFF;

static void draw_hud(const BonusGame *b)
{
    char line[40];
    u16 g = b->gems, t = (b->endTime > b->ticks) ? (u16)((b->endTime - b->ticks) / 1000) : 0;
    /* the text only changes when a gem is taken or once a second */
    if (g == hudShownGems && b->items == hudShownItems && t == hudShownTime) return;
    hudShownGems = g; hudShownItems = b->items; hudShownTime = t;
    u16 secs = t % 60, mins = t / 60;
    line[0] = 'G'; line[1] = 'E'; line[2] = 'M'; line[3] = 'S'; line[4] = ' ';
    line[5] = (char)('0' + (g / 10) % 10); line[6] = (char)('0' + g % 10); line[7] = '/';
    line[8] = (char)('0' + (b->items / 10) % 10); line[9] = (char)('0' + b->items % 10);
    line[10] = ' '; line[11] = ' '; line[12] = 'T'; line[13] = 'I'; line[14] = 'M'; line[15] = 'E'; line[16] = ' ';
    line[17] = (char)('0' + mins % 10); line[18] = ':';
    line[19] = (char)('0' + secs / 10); line[20] = (char)('0' + secs % 10); line[21] = ' '; line[22] = 0;
    VDP_drawTextBG(WINDOW, line, 8, 0);
#ifdef BONUS_FPS
    hudShownTime = 0xFFFF;
    { char f[8]; intToStr(SYS_getFPS(), f, 1); VDP_drawTextBG(WINDOW, f, 1, 0); }
#endif
}

static u8 bonus_input(u16 pad)
{
    u8 in = 0;
    if (pad & BUTTON_UP) in |= BONUS_INPUT_UP;
    if (pad & BUTTON_DOWN) in |= BONUS_INPUT_DOWN;
    if (pad & BUTTON_LEFT) in |= BONUS_INPUT_LEFT;
    if (pad & BUTTON_RIGHT) in |= BONUS_INPUT_RIGHT;
    if (pad & (jj1_controls == 1 ? BUTTON_B : jj1_controls == 2 ? BUTTON_C : BUTTON_A)) in |= BONUS_INPUT_RUN;
    if (pad & (jj1_controls == 1 ? (BUTTON_A | BUTTON_C) : jj1_controls == 2 ? (BUTTON_A | BUTTON_B) : (BUTTON_B | BUTTON_C))) in |= BONUS_INPUT_JUMP;
    return in;
}

static void play_bonus_sound(u8 se)
{
    /* event sound ids are the original's slot numbers: 3 = fanfare, 4 = "ow" */
    if (se == 3) jj1_sfx_start(SFX_BONUS_GOD, 2, SOUND_PCM_CH4);
    else if (se == 4) jj1_sfx_start(SFX_BONUS_OW, 2, SOUND_PCM_CH4);
}

#ifdef BONUS_SELFTEST
/* Every angle at several positions: the assembly rows must equal the C
 * reference word for word.  Shows the result and stops. */
static void floor_selftest(void)
{
    static u16 ref[2][FLOOR_ROWS][PLANE_W];
    u32 bad = 0, cases = 0;
    u16 dir;
    u8 p;
    char line[40];
    static const s32 pos[6][2] = {
        { 0x00104000L, 0x00204000L }, { 0x007FC123L, 0x00000321L }, { 0x00000001L, 0x007FFFFFL },
        { 0x0034A5A5L, 0x0012F0F0L }, { 0x00400000L, 0x00400000L }, { 0x007ABCDEL, 0x00654321L }
    };
    static BonusGame saved;
    saved = bg;
    VDP_drawTextBG(WINDOW, "FLOOR SELFTEST...", 1, 0);
    for (p = 0; p < 6; p++) {
        bg.x = pos[p][0]; bg.y = pos[p][1];
        for (dir = 0; dir < 1024; dir++) {
            u16 i, *a = &floorMap[0][0][0], *c = &ref[0][0][0];
            bg.direction = dir;
            render_floor_c(&bg);
            memcpy(ref, floorMap, sizeof(ref));
            render_floor(&bg);
            for (i = 0; i < 2 * FLOOR_ROWS * PLANE_W; i++) if (a[i] != c[i]) bad++;
            cases++;
        }
    }
    bg = saved;
    strcpy(line, bad ? "FLOOR SELFTEST BAD " : "FLOOR SELFTEST OK  ");
    intToStr((s32)(bad > 99999 ? 99999 : bad), line + 19, 1);
    VDP_drawTextBG(WINDOW, line, 1, 0);
    intToStr((s32)cases, line, 1);
    VDP_drawTextBG(WINDOW, line, 1, 1);
    /* result for tests/run_headless_emulator_test.sh: 'F','L','O','R', 1 = OK */
    SRAM_enable();
    SRAM_writeByte(0, 'F'); SRAM_writeByte(1, 'L'); SRAM_writeByte(2, 'O'); SRAM_writeByte(3, 'R');
    SRAM_writeByte(4, bad ? 2 : 1);
    SRAM_disable();
    while (TRUE) SYS_doVBlankProcess();
}
#endif

int bonus_play(u8 index, const u8 *music)
{
    /* Lite cartridge: episode 1's three maps, in the program. */
    static const u8 *const grids[3] = { jj1_bonus_grid0, jj1_bonus_grid1, jj1_bonus_grid2 };
    static const u8 *const events[3] = { jj1_bonus_events0, jj1_bonus_events1, jj1_bonus_events2 };
    int result = BONUS_CONTINUE;
    u16 lastTimer, x, y, whiteFrames = 0, frame = 0;
    const u16 whiteDuration = SYS_isPAL() ? 100 : 120; /* original 2000 ms */
    u16 originalPalettes[64];
    u8 k, row;
#ifdef BONUS_PROF
    u32 pScan0 = 0, pScanT = 0, pSortT = 0, pBusy0 = 0, pBusyT = 0, pWaitT = 0;
    u32 pt0 = 0, pT[4] = { 0, 0, 0, 0 }, steps0 = 0;
#endif
    u8 loadedPlayerFrame = 0xFF;

    if (index > 2) index = 0;
    bonus_init(&bg, index, jj1_bonus_tiles, jj1_bonus_masks, grids[index], events[index]);
    for (k = 0; k < 60; k++) {
        tbase[k] = bg.floorTiles + ((u16)k << 10);
        tbaseHi[k] = jj1_bonus_tiles_hi + ((u16)k << 10);
    }
    hudShownTime = 0xFFFF;
#ifdef BONUS_DEBUG_X
    bg.x = ((s32)BONUS_DEBUG_X << 15) + 16384; bg.y = ((s32)BONUS_DEBUG_Y << 15) + 16384;
#endif

    /* display set-up */
    VDP_setScreenWidth320();
    VDP_setPlaneSize(64, 32, TRUE);
    VDP_setScrollingMode(HSCROLL_TILE, VSCROLL_PLANE);
    VDP_clearPlane(BG_A, TRUE); VDP_clearPlane(BG_B, TRUE); VDP_clearPlane(WINDOW, TRUE);
    VDP_clearSprites(); VDP_updateSprites(1, DMA);
    VDP_setHorizontalScroll(BG_A, 0); VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_A, -4); VDP_setVerticalScroll(BG_B, 0);
    PAL_setPalette(PAL1, jj1_bonus_palette, CPU);
    VDP_setBackgroundColor(0);
    VDP_setWindowOnTop(2);
    VDP_setTextPlane(WINDOW);
    DMA_setMaxTransferSize(16384);

    PAL_setPalette(PAL2, jj1_bonus_sky_palette, CPU);
    PAL_setPalette(PAL3, jj1_bonus_player_palette, CPU);
    for (k = 0; k < 15; k++) {
        u32 sky[8]; u8 j;
        for (j = 0; j < 8; j++) sky[j] = 0x11111111UL * (u32)(k + 1);
        VDP_loadTileData(sky, T_SKY + k, 1, CPU);
    }
    VDP_loadTileData((const u32 *)jj1_bonus_bg, T_STRIP, 192, CPU);
    {
        u8 it, sz;
        static const u32 *const tbl[4][4] = {
            { jj1_bonus_item1_s0, jj1_bonus_item1_s1, jj1_bonus_item1_s2, jj1_bonus_item1_s3 },
            { jj1_bonus_item2_s0, jj1_bonus_item2_s1, jj1_bonus_item2_s2, jj1_bonus_item2_s3 },
            { jj1_bonus_item3_s0, jj1_bonus_item3_s1, jj1_bonus_item3_s2, jj1_bonus_item3_s3 },
            { jj1_bonus_item4_s0, jj1_bonus_item4_s1, jj1_bonus_item4_s2, jj1_bonus_item4_s3 },
        };
        static const u8 cnt[4] = { 16, 9, 4, 1 };
        /* the table is indexed by event id 1..4: clock, gem, glove, exit */
        for (it = 0; it < 4; it++) {
            u16 at = T_ITEMS + it * ITEM_TILES;
            for (sz = 0; sz < 4; sz++) { VDP_loadTileData(tbl[it][sz], at, cnt[sz], CPU); at += cnt[sz]; }
        }
    }

    /* sky rows, background strip, floor map */
    for (y = 0; y < 15; y++)
        for (x = 0; x < 64; x++)
            VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL2, FALSE, FALSE, FALSE, T_SKY + y), x, y);
    for (y = 0; y < 3; y++)
        for (x = 0; x < 64; x++)
            VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE, T_STRIP + y * 64 + x), x, 15 + y);
    load_pair_tiles();
    {   /* hscroll per 8-line band: everything still at 0 */
        static const s16 zero[28] = { 0 };
        VDP_setHorizontalScrollTile(BG_A, 0, (s16 *)zero, 28, CPU);
        VDP_setHorizontalScrollTile(BG_B, 0, (s16 *)zero, 28, CPU);
    }

    /* per-row distances of the perspective floor (original formula) */
    for (row = 0; row < SAMPLES_Y; row++) {
        s32 yo = 85 - (s32)row * 84 / (SAMPLES_Y - 1);
        rowDist[row] = (s32)(((819200L) << 0) / (92 - yo));     /* 800/(92-y) in 10-bit fixed point */
    }

#ifdef BONUS_SELFTEST
    floor_selftest();
#endif
    PAL_getColors(0, originalPalettes, 64);
    jj1_music_start(music);
    lastTimer = vtimer;
#ifdef BONUS_FORCE_RESULT
    result = BONUS_FORCE_RESULT; /* deterministic integration-test build */
#endif

    while (TRUE) {
#ifdef BONUS_PROF
        pt0 = getSubTick();
#endif
        u16 pad = JOY_readJoypad(JOY_1);
        u16 now = vtimer, elapsed = (u16)(now - lastTimer), steps = elapsed, count = 0;
        s32 zpx;
        lastTimer = now;
        if (steps < 1) steps = 1;
#ifdef BONUS_FIXSTEP
        steps = (frame >= BONUS_STOPAT) ? 0 : BONUS_FIXSTEP;
#endif
        if (steps > 16) steps = 16;
#ifdef BONUS_FREEZE
        steps = 0;      /* reproducible frames for comparing renderers */
#endif
#ifdef BONUS_PROF
        steps0 = steps;
#endif

        if (result == BONUS_CONTINUE) {
            while (steps--) {
                result = bonus_step(&bg, bonus_input(pad));
                play_bonus_sound(bg.sound);
                if (result != BONUS_CONTINUE) break;
            }
        } else {
            /* end of level: spin and fade to white, as the original does */
            if (whiteFrames >= whiteDuration) break;
            if (!elapsed) elapsed = 1;
            bg.direction += 12 + (whiteFrames >> 1);
            whiteFrames = (u16)(whiteFrames + elapsed);
            if (whiteFrames > whiteDuration) whiteFrames = whiteDuration;
        }
        frame++;

#ifdef BONUS_PROF
        pT[0] += getSubTick() - pt0;
        pt0 = getSubTick();
#endif
#ifndef BONUS_NOFLOOR
#ifdef BONUS_C_FLOOR
        render_floor_c(&bg);
#else
        render_floor(&bg);
#endif
#endif
#ifdef BONUS_PROF
        pT[1] += getSubTick() - pt0;
        pt0 = getSubTick();
#endif
        draw_hud(&bg);
        {   /* sky and mountain strip turn with the player; the floor rows don't */
            static s16 turn[FLOOR_TOP_ROW];
            u8 i;
            for (i = 0; i < FLOOR_TOP_ROW; i++) turn[i] = (s16)-bg.direction;
            VDP_setHorizontalScrollTile(BG_B, 0, turn, FLOOR_TOP_ROW, DMA_QUEUE);
        }

        /* OpenJazz maps player states to BONUSMAP animation slots 0..6.
         * Advance at 75 ms/frame; each slot retains its original frame list. */
        {
            u8 anim = bg.crashed ? 5 : bg.z > 0 ? 4 :
                      (pad & BUTTON_LEFT) ? 1 : (pad & BUTTON_RIGHT) ? 2 :
                      (pad & (BUTTON_UP | BUTTON_DOWN)) ? 0 : 6;
            u8 n = jj1_bonus_player_counts[index][anim];
            u8 which = jj1_bonus_player_anims[index][anim][(bg.ticks / 75) % n];
            if (which != loadedPlayerFrame) {
                VDP_loadTileData(jj1_bonus_player_frames[which], T_HERO, HERO_TILES, DMA_QUEUE);
                loadedPlayerFrame = which;
            }
        }
        zpx = (bg.z * 80) >> 10;
        count = draw_player(count, (s16)zpx);
#ifndef BONUS_NOITEMS
#ifdef BONUS_PROF
        pScan0 = getSubTick();
#endif
        count = draw_items(&bg, count);
#ifdef BONUS_PROF
        pScanT += profScan - pScan0; pSortT += getSubTick() - profScan;
#endif
#endif
        VDP_setSpriteLink(count - 1, 0);
        VDP_updateSprites(count, DMA_QUEUE);
#ifdef BONUS_PROF
        pT[2] += getSubTick() - pt0;
        pT[3] += steps0;
        if (!(frame & 7)) {
            char b[40]; u8 i, n = 0;
            for (i = 0; i < 4; i++) {
                u16 v = (u16)((pT[i] / 8) * 13 / 100);
                if (i == 3) v = (u16)(pT[3] / 8 * 10);
                b[n++] = (char)('0' + v / 100 % 10); b[n++] = (char)('0' + v / 10 % 10); b[n++] = (char)('0' + v % 10); b[n++] = ' ';
                pT[i] = 0;
            }
            {
                u16 v1 = (u16)((pScanT / 8) * 13 / 100), v2 = (u16)((pSortT / 8) * 13 / 100);
                b[n++] = (char)('0' + v1 / 10 % 10); b[n++] = (char)('0' + v1 % 10); b[n++] = ' ';
                b[n++] = (char)('0' + v2 / 10 % 10); b[n++] = (char)('0' + v2 % 10); b[n++] = ' ';
                b[n++] = (char)('0' + profItems / 10 % 10); b[n++] = (char)('0' + profItems % 10);
                {
                    u16 v3 = (u16)((pBusyT / 8) * 13 / 100), v4 = (u16)((pWaitT / 8) * 13 / 100);
                    b[n++] = ' '; b[n++] = (char)('0' + v3 / 100 % 10); b[n++] = (char)('0' + v3 / 10 % 10); b[n++] = (char)('0' + v3 % 10);
                    b[n++] = ' '; b[n++] = (char)('0' + v4 / 100 % 10); b[n++] = (char)('0' + v4 / 10 % 10); b[n++] = (char)('0' + v4 % 10);
                    pBusyT = pWaitT = 0;
                }
                pScanT = pSortT = 0;
            }
            b[n] = 0; VDP_drawTextBG(WINDOW, b, 8, 1);
        }
#endif

        if (result != BONUS_CONTINUE) {
            /* White out the entire frame, including sky and HUD. Timing is
             * based on VBlanks rather than slow perspective-render frames. */
            u16 pal[16], bank, i, lvl = (whiteFrames * 7) / whiteDuration;
            for (bank = 0; bank < 4; bank++) {
                for (i = 0; i < 16; i++) {
                    u16 color = originalPalettes[bank * 16 + i];
                    u16 r = (color >> 1) & 7, g = (color >> 5) & 7, bl = (color >> 9) & 7;
                    r += ((7 - r) * lvl) / 7; g += ((7 - g) * lvl) / 7; bl += ((7 - bl) * lvl) / 7;
                    pal[i] = (u16)((bl << 9) | (g << 5) | (r << 1));
                }
                PAL_setPalette(bank, pal, DMA);
            }
        }
#ifndef BONUS_NODMA
        floor_upload();
#endif
#ifdef BONUS_PROF
        { u32 t = getSubTick(); pBusyT += t - pBusy0; pBusy0 = t; }
#endif
        SYS_doVBlankProcess();
#ifdef BONUS_PROF
        { u32 t = getSubTick(); pWaitT += t - pBusy0; pBusy0 = t; }
#endif
    }

    DMA_setMaxTransferSizeToDefault();
    XGM_stopPlay();
    VDP_clearSprites(); VDP_updateSprites(1, DMA);
    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_A, 0);
    return result;
}
