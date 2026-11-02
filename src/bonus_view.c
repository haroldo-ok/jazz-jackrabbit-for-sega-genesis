/* Genesis front end for the JJ1 bonus levels.
 *
 * The floor is the original's perspective texture map, sampled at 80x20 and
 * written 4x4 pixels per sample straight into a RAM tile buffer that is
 * DMA'd to VRAM each frame.  Items are the original gem/clock/glove/exit
 * graphics, pre-scaled to four sprite sizes. The player is the original
 * 72x88 bonus animation, streamed as a nine-sprite VDP composite. */
#include <genesis.h>
#include "jj1_options.h"
#include "jazz_gfx.h"
#include "bonus_view.h"
#include "jj1_bonus.h"
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
#define T_STRIP   (T_FLOOR + FLOOR_TILES)
#define T_ITEMS   (T_STRIP + 192)
#define ITEM_TILES 30               /* 16 + 9 + 4 + 1 per item type */
#define T_HERO    (T_ITEMS + 4 * ITEM_TILES)
#define HERO_TILES 99
#define T_SKY     (T_HERO + HERO_TILES)    /* 15 solid tiles, one per gradient row */

typedef char bonus_vram_fits[(T_SKY + 15 <= JJ1_VRAM_TILE_LIMIT) ? 1 : -1];

#define SFX_BONUS_GOD 77            /* XGM PCM ids; the main game uses 64-76 */
#define SFX_BONUS_OW  68            /* shared with the main game's "ow" */

#define MUL(x, y) ((s32)(((s32)(x) * (s32)(y)) >> 10))

static u8 floorBuf[FLOOR_TILES * 32] __attribute__((aligned(2)));
static u16 quadTab[16];     /* palette index -> four identical 4bpp pixels */
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

static void render_floor(const BonusGame *b)
{
    const s32 sn = bonus_sin(b->direction), cs = bonus_cos(b->direction);
    const u8 *tbase[60];
    const u8 *grid = b->grid;
    u8 sy, t;
    for (t = 0; t < 60; t++) tbase[t] = b->floorTiles + ((u16)t << 10);
    for (sy = 0; sy < SAMPLES_Y; sy++) {
        s32 d = rowDist[sy];
        s32 sideX = MUL(d, cs), sideY = MUL(d, sn);
        /* positions in 16.16 so the pixel is simply the high word */
        s32 u = (b->x + MUL(d - 16384, sn) - (sideX >> 1)) << 6;
        s32 v = (b->y - MUL(d - 16384, cs) - (sideY >> 1)) << 6;
        s32 du = (sideX / SAMPLES_X) << 6, dv = (sideY / SAMPLES_X) << 6;
        /* sample row sy covers pixel rows 4*sy..4*sy+3: tile row sy>>1, rows (sy&1)*4.. */
        u8 *o = floorBuf + ((((sy >> 1) * FLOOR_COLS) * 8) + ((sy & 1) * 4)) * 4;
        u8 n;
        u16 lastKey = 0xFFFF;
        const u8 *tp = tbase[0];
        for (n = 0; n < FLOOR_COLS; n++) {
            u8 k;
            for (k = 0; k < 2; k++) {
                /* High word of the 16.16 coordinate; avoid type-punning. */
                u16 px = (u16)((u32)u >> 16) & 8191, py = (u16)((u32)v >> 16) & 8191;
                u16 key = ((py & 0x1FE0) << 3) | (px >> 5);
                u16 quad;
                u16 *w;
                /* neighbouring samples almost always share a floor tile */
                if (key != lastKey) { lastKey = key; tp = tbase[grid[key]]; }
                quad = quadTab[tp[((py & 31) << 5) | (px & 31)]];
                w = (u16 *)(o + k * 2);
                w[0] = quad; w[2] = quad; w[4] = quad; w[6] = quad;
                u += du; v += dv;
            }
            o += 32;
        }
    }
}

typedef struct { s16 x; u8 ev; u8 size; s32 div; } Seen;

static u16 draw_items(const BonusGame *b, u16 count)
{
    Seen seen[40];
    u8 n = 0, i, j;
    const s32 sn = bonus_sin(b->direction), cs = bonus_cos(b->direction);
    s32 px = b->x, py = b->y;
    int y, x;
    for (y = -6; y < 6; y++) {
        int evY = (((b->direction - 256) & 512) ? y : -y);
        s32 sY = ((s32)evY << 15) + 16384 - (py & 32767);
        for (x = -6; x < 6; x++) {
            int evX = ((b->direction & 512) ? x : -x);
            s32 sX = ((s32)evX << 15) + 16384 - (px & 32767);
            s32 div = 16384 + MUL(sX, sn) - MUL(sY, cs);
            u8 ev;
            if ((div >> 10) <= 8) continue;
            ev = bonus_event(b, evX + (int)(px >> 15), evY + (int)(py >> 15));
            if (ev && b->evType[ev & 15] > 0 && b->evType[ev & 15] != 5 && n < 40) {
                s32 nX = ((MUL(sX, cs) + MUL(sY, sn)) << 10) / div;
                s32 scr = ((nX * 320) >> 10) + 160;
                s32 scale = (s32)((65536L << 10) / div);          /* 1024 = 1.0 */
                s32 pxsize = (24 * scale) >> 10;
                u8 size = (pxsize >= 28) ? 0 : (pxsize >= 20) ? 1 : (pxsize >= 12) ? 2 : 3;
                if (scr > -40 && scr < 360) {
                    seen[n].x = (s16)scr; seen[n].ev = ev; seen[n].size = size; seen[n].div = div; n++;
                }
            }
        }
    }
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

static void draw_hud(const BonusGame *b)
{
    char line[40];
    u16 g = b->gems, t = (b->endTime > b->ticks) ? (u16)((b->endTime - b->ticks) / 1000) : 0;
    u16 secs = t % 60, mins = t / 60;
    line[0] = 'G'; line[1] = 'E'; line[2] = 'M'; line[3] = 'S'; line[4] = ' ';
    line[5] = (char)('0' + (g / 10) % 10); line[6] = (char)('0' + g % 10); line[7] = '/';
    line[8] = (char)('0' + (b->items / 10) % 10); line[9] = (char)('0' + b->items % 10);
    line[10] = ' '; line[11] = ' '; line[12] = 'T'; line[13] = 'I'; line[14] = 'M'; line[15] = 'E'; line[16] = ' ';
    line[17] = (char)('0' + mins % 10); line[18] = ':';
    line[19] = (char)('0' + secs / 10); line[20] = (char)('0' + secs % 10); line[21] = ' '; line[22] = 0;
    VDP_drawTextBG(WINDOW, line, 8, 0);
#ifdef BONUS_FPS
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

int bonus_play(u8 index, const u8 *music)
{
    static const u8 *const grids[3] = { jj1_bonus_grid0, jj1_bonus_grid1, jj1_bonus_grid2 };
    static const u8 *const events[3] = { jj1_bonus_events0, jj1_bonus_events1, jj1_bonus_events2 };
    int result = BONUS_CONTINUE;
    u16 lastTimer, x, y, whiteFrames = 0, frame = 0;
    const u16 whiteDuration = SYS_isPAL() ? 100 : 120; /* original 2000 ms */
    u16 originalPalettes[64];
    u8 k, row;
    u8 loadedPlayerFrame = 0xFF;

    for (k = 0; k < 16; k++) quadTab[k] = (u16)(k | (k << 4) | (k << 8) | (k << 12));
    if (index > 2) index = 0;
    bonus_init(&bg, index, jj1_bonus_tiles, jj1_bonus_masks, grids[index], events[index]);
#ifdef BONUS_DEBUG_X
    bg.x = ((s32)BONUS_DEBUG_X << 15) + 16384; bg.y = ((s32)BONUS_DEBUG_Y << 15) + 16384;
#endif

    /* display set-up */
    VDP_setScreenWidth320();
    VDP_setPlaneSize(64, 32, TRUE);
    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_clearPlane(BG_A, TRUE); VDP_clearPlane(BG_B, TRUE); VDP_clearPlane(WINDOW, TRUE);
    VDP_clearSprites(); VDP_updateSprites(1, DMA);
    VDP_setHorizontalScroll(BG_A, 0); VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_A, 0); VDP_setVerticalScroll(BG_B, 0);
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
    for (y = 0; y < FLOOR_ROWS; y++)
        for (x = 0; x < FLOOR_COLS; x++)
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE, T_FLOOR + y * FLOOR_COLS + x),
                             x, FLOOR_TOP_ROW + y);

    /* per-row distances of the perspective floor (original formula) */
    for (row = 0; row < SAMPLES_Y; row++) {
        s32 yo = 85 - (s32)row * 84 / (SAMPLES_Y - 1);
        rowDist[row] = (s32)(((819200L) << 0) / (92 - yo));     /* 800/(92-y) in 10-bit fixed point */
    }

    PAL_getColors(0, originalPalettes, 64);
    jj1_music_start(music);
    lastTimer = vtimer;
#ifdef BONUS_FORCE_RESULT
    result = BONUS_FORCE_RESULT; /* deterministic integration-test build */
#endif

    while (TRUE) {
        u16 pad = JOY_readJoypad(JOY_1);
        u16 now = vtimer, elapsed = (u16)(now - lastTimer), steps = elapsed, count = 0;
        s32 zpx;
        lastTimer = now;
        if (steps < 1) steps = 1;
        if (steps > 16) steps = 16;

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

#ifndef BONUS_NOFLOOR
        render_floor(&bg);
#endif
        draw_hud(&bg);
        VDP_setHorizontalScroll(BG_B, -bg.direction);

        /* OpenJazz maps player states to BONUSMAP animation slots 0..6.
         * Advance at 75 ms/frame; each slot retains its original frame list. */
        {
            u8 anim = bg.crashed ? 5 : bg.z > 0 ? 4 :
                      (pad & BUTTON_LEFT) ? 1 : (pad & BUTTON_RIGHT) ? 2 :
                      (pad & (BUTTON_UP | BUTTON_DOWN)) ? 0 : 6;
            u8 n = jj1_bonus_player_counts[index][anim];
            u8 which = jj1_bonus_player_anims[index][anim][(bg.ticks / 75) % n];
            if (which != loadedPlayerFrame) {
                VDP_loadTileData(jj1_bonus_player_frames[which], T_HERO, HERO_TILES, CPU);
                loadedPlayerFrame = which;
            }
        }
        zpx = (bg.z * 80) >> 10;
        count = draw_player(count, (s16)zpx);
#ifndef BONUS_NOITEMS
        count = draw_items(&bg, count);
#endif
        VDP_setSpriteLink(count - 1, 0);
        VDP_updateSprites(count, DMA_QUEUE);

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
        DMA_queueDma(DMA_VRAM, floorBuf, T_FLOOR * 32, FLOOR_TILES * 16, 2);
#endif
        SYS_doVBlankProcess();
    }

    DMA_setMaxTransferSizeToDefault();
    XGM_stopPlay();
    VDP_clearSprites(); VDP_updateSprites(1, DMA);
    VDP_setHorizontalScroll(BG_B, 0);
    return result;
}
