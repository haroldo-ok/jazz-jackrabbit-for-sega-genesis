#include <genesis.h>
#include "jj1_options.h"
#include "jj1_save.h"
#include "jazz_game.h"
#include "jazz_gfx.h"
#include "jj1_events.h"
#include "jj1_runtime.h"
#include "jj1_sounds.h"
#include "jj1_music.h"
#include "jj1_font.h"
#include "jj1_hud.h"
#include "bonus_view.h"
#include "stage_cheat.h"
#include "jj1_scenes.h"
#include "jj1_pack.h"

#include "jj1_sfx.h"

#define SCREEN_W 320
#define PLAYER_W 14
#define PLAYER_H 22

typedef enum { MODE_TITLE, MODE_MENU, MODE_PLAY, MODE_GAMEOVER, MODE_COMPLETE } GameMode;

static JazzGame game;
static GameMode mode;
static s16 cameraX;
static s16 cameraY;
static s16 lastMapColumn;
static u8 renderedStage;
static u16 renderedDestroyCount;
/* Which (event, frame) each enemy VRAM slot currently holds; 0xFFFF = unknown. */
static u16 enemySlotArt[JAZZ_MAX_ENEMIES];
static u16 playerSlotFrame;
static u16 boardSlotKey;
static u16 bossGunArt;
static u16 lastInput;
#ifdef JAZZ_PROFILE
static u32 profT[8];
#ifdef JAZZ_PROFDUMP
static u32 profSum[8], profBusy, profIter, profCur[8], profOver[8], profOverN;
static u16 profBusyMax, profHist[6], profDmaMax, profDmaOver;
#define PROF(n, stmt) do { u32 t0 = getSubTick(), t1; stmt; t1 = getSubTick() - t0; profT[n] += t1; profSum[n] += t1; profCur[n] += t1; } while (0)
#else
#define PROF(n, stmt) do { u32 t0 = getSubTick(); stmt; profT[n] += getSubTick() - t0; } while (0)
#endif
#else
#define PROF(n, stmt) stmt
#endif

/* Animation frame (game.frame / 8) mod n, and small products, with the
 * 68000's 16-bit divide and multiply: written with int arithmetic GCC calls
 * libgcc's 32-bit __modsi3 / __mulsi3 routines, several per sprite. */
static inline u8 anim_frame(u8 n)
{
    u32 x = (u32)(game.frame >> 3);
    if (n < 2) return 0;
    __asm__("divu.w %1,%0" : "+d"(x) : "d"((u16)n));
    return (u8)(x >> 16);
}

static inline u16 mul_u8(u8 a, u8 b)
{
    u32 r = a;
    __asm__("mulu.w %1,%0" : "+d"(r) : "d"((u16)b));
    return (u16)r;
}

static u16 itemSlotLoaded[JJ1_ITEM_SLOTS] = { 0xFFFF, 0xFFFF };

static u16 approachedPlanet = 0xFFFF;
static u8 selectedStage;   /* level select on the title screen */
static u8 cheatUnlocked = 0;   /* unlocked by the pad code until reset */
static u8 cheatProgress;   /* how many pad-code steps have been matched */
static u8 menuOption;
static u16 titlePreviousPad;
static u16 playPreviousPad;


static u16 input_from_pad(u16 pad)
{
    u16 input = 0;
    if (pad & BUTTON_LEFT) input |= JAZZ_INPUT_LEFT;
    if (pad & BUTTON_RIGHT) input |= JAZZ_INPUT_RIGHT;
    if (pad & (jj1_controls == 1 ? BUTTON_A : BUTTON_B)) input |= JAZZ_INPUT_JUMP;
    if (pad & (jj1_controls == 1 ? BUTTON_B : jj1_controls == 2 ? BUTTON_C : BUTTON_A)) input |= JAZZ_INPUT_FIRE;
    if (pad & (jj1_controls == 2 ? BUTTON_A : BUTTON_C)) input |= JAZZ_INPUT_SWITCH;
    if (pad & BUTTON_DOWN) input |= JAZZ_INPUT_DOWN;  /* crouch / airboard down */
    if (pad & BUTTON_UP) input |= JAZZ_INPUT_UP;      /* look up / airboard up */
    if (pad & BUTTON_START) input |= JAZZ_INPUT_START;
    return input;
}


/* The HUD lives on the window plane over the top two rows, and this runs
 * right after vblank - i.e. while the beam is drawing exactly those rows.
 * Clearing and rewriting them every frame therefore raced the raster: when
 * a streamed block column delayed the redraw, the beam passed rows 0-1 while
 * they were still blank and the HUD flickered out. So: never clear, build
 * fixed-width space-padded lines, and only touch VRAM when a line actually
 * changes (score/gems/health move rarely, so the window is stable). */
/* The status panel: two rows of the window plane built from tiles cut out of
 * PANEL.000 (tools/build_sgdk_panel.py).  The tiles live in the lowercase
 * slots of the font (the text is all uppercase), so they cost no extra VRAM.
 * The panel palette keeps colour 1 = black and 15 = white, so the 8x8 font
 * still draws weapon and message text on top of it. */
#define HUD_BASE   (TILE_FONT_INDEX + 65)
#define HUD_PLATE  0
#define HUD_BLACK  1
#define HUD_DIGIT0 2
#define HUD_HEART_FULL  12
#define HUD_HEART_EMPTY 13
#define HUD_LIVES  14
#define HUD_PLANET 18
#define HUD_ATTR(i) TILE_ATTR_FULL(PAL0, 1, 0, 0, HUD_BASE + (i))

static u16 hudMap[80];
static u32 hudKey = 0xFFFFFFFFUL;

static void hud_enter(void)
{
    VDP_loadTileData(jj1_hud.tiles, HUD_BASE, 22, DMA);
    PAL_setPalette(PAL0, jj1_hud_pal.data, DMA);
    hudKey = 0xFFFFFFFFUL;
}

static void hud_leave(void)
{
    PAL_setPalette(PAL0, jj1_font_pal.data, DMA);
}

static void hud_text(u8 row, u8 x, const char *t)
{
    while (*t && x < 40) {
        hudMap[row * 40 + x] = TILE_ATTR_FULL(PAL0, 1, 0, 0, TILE_FONT_INDEX + (u8)(*t - 32));
        t++; x++;
    }
}

static void hud_number(u8 x, u8 digits, u16 value)
{
    u8 i;
    for (i = digits; i > 0; i--) {
        hudMap[x + i - 1] = HUD_ATTR(HUD_DIGIT0 + (value % 10));
        value /= 10;
    }
}

static void draw_status_line(const JazzGame *g)
{
    u32 key = g->score * 31UL + g->lives + ((u32)g->stageGems << 8) +
              ((u32)g->stageGemTotal << 16) + ((u32)g->health << 24) +
              ((u32)g->player.shotType << 27) + (g->paused ? 0x40000000UL : 0) +
              (g->transitionTimer ? 0x20000000UL : 0) + ((u32)jj1_controls << 20);
    u8 i;
    if (key == hudKey) return;
    hudKey = key;

    for (i = 0; i < 80; i++) hudMap[i] = HUD_ATTR(HUD_PLATE);
    /* score box: 8 digits, low five then high three */
    hud_number(4, 5, (u16)(g->score % 100000UL));
    hud_number(1, 3, (u16)((g->score / 100000UL) % 1000UL));
    /* gems: icon, collected, total */
    hudMap[10] = HUD_ATTR(HUD_PLANET);     hudMap[11] = HUD_ATTR(HUD_PLANET + 1);
    hudMap[50] = HUD_ATTR(HUD_PLANET + 2); hudMap[51] = HUD_ATTR(HUD_PLANET + 3);
    hud_number(12, 3, g->stageGems);
    hud_number(16, 3, g->stageGemTotal);
    /* lives */
    hudMap[20] = HUD_ATTR(HUD_LIVES);      hudMap[21] = HUD_ATTR(HUD_LIVES + 1);
    hudMap[60] = HUD_ATTR(HUD_LIVES + 2);  hudMap[61] = HUD_ATTR(HUD_LIVES + 3);
    hud_number(22, 2, g->lives);
    /* health hearts */
    for (i = 0; i < 5; i++)
        hudMap[40 + 1 + i] = HUD_ATTR((g->health > i) ? HUD_HEART_FULL : HUD_HEART_EMPTY);
    /* weapon (row 0) */
    {
        static const char *const weaponText[JAZZ_SHOT_TYPES] = {
            "BLAST", "TOAST", "UP", "BOUNCE", "SPCL"
        };
        hud_text(0, 24, "WPN:");
        hud_text(0, 28, weaponText[(g->player.shotType < JAZZ_SHOT_TYPES) ? g->player.shotType : 0]);
    }
    /* message (row 1) */
    if (g->paused)               hud_text(1, 24, "START  C SAVE");
    else if (g->transitionTimer) hud_text(1, 24, "EXIT FOUND!");
    else {
        static const char *const keysText[3] = { "A FIRE B JUMP", "B FIRE A JUMP", "C FIRE B JUMP" };
        hud_text(1, 24, keysText[jj1_controls % 3]);
    }
    VDP_setTileMapDataRect(WINDOW, hudMap, 0, 0, 40, 2, 40, DMA_QUEUE);
}




static void render_level_map(void)
{
    /* The foreground is populated directly by render_backdrop() from the
       decoded LEVEL/BLOCKS chunk. Keep the gameplay camera anchored until the
       full 256x64 chunk streamer replaces this resident screen. */
    cameraX = 0;
    lastMapColumn = 0;
    VDP_setHorizontalScroll(BG_A, 0);
}

static u8 jj1BlockCache[JJ1_BLOCK_CACHE_BLOCKS];
static u8 jj1BlockSlot[256];        /* reverse of jj1BlockCache: slot or 0xFF */
static u8 jj1Visible[256];          /* == jj1VisStamp: in the 11x8 window */
static u8 jj1VisStamp, jj1VisValid;
static u8 jj1CacheReplace;
static s16 jj1MapOriginX = -1;
static s16 jj1MapOriginY = -1;
static const Jj1StageArt *stageArt;
static const u32 *jj1ActiveBlockTiles;
static const u8 *jj1ActiveBlocks;
static const u16 *jj1ActiveBlockIndex;

/* Sprite and player frames.  On the lite cartridge the level's tiles are a
 * pool of distinct tiles plus an index per logical tile, so a frame is
 * gathered tile by tile into the DMA queue's own buffer. */
static u32 liteScratch[64 * 8];
/* The index (tools/build_cartridge.py pack_index): u16 groups, u16 exceptions,
 * u16 base per 16 logical tiles, u8 offset per tile (255: exception), then
 * sorted (logical, tile) pairs. */
static u16 lite_exception(const u16 *index, u16 logical)
{
    const u16 groups = index[0];
    const u16 *exc = index + 2 + groups + (groups << 3);
    u16 lo = 0, hi = index[1];
    while (lo < hi) {
        const u16 mid = (u16)((lo + hi) >> 1);
        const u16 at = exc[mid << 1];
        if (at == logical) return exc[(mid << 1) + 1];
        if (at < logical) lo = (u16)(mid + 1); else hi = mid;
    }
    return 0;
}

static void lite_tiles(const u32 *pool, const u16 *index, u16 first, u16 vram, u16 count, TransferMethod tm)
{
    const u16 *base = index + 2;
    const u8 *offset = (const u8 *)(base + index[0]);
    while (count) {
        u16 n = (count > 64) ? 64 : count, i;
        u32 *dst = (tm == DMA_QUEUE) ?
            (u32 *)DMA_allocateAndQueueDma(DMA_VRAM, vram * 32, n * 16, 2) : 0;
        u32 *out = dst ? dst : liteScratch;
        for (i = 0; i < n; i++) {
            const u16 logical = (u16)(first + i);
            const u8 o = offset[logical];
            const u16 tile = (o != 255) ? (u16)(base[logical >> 4] + o) : lite_exception(index, logical);
            const u32 *t = pool + ((u32)tile << 3);
            out[0] = t[0]; out[1] = t[1]; out[2] = t[2]; out[3] = t[3];
            out[4] = t[4]; out[5] = t[5]; out[6] = t[6]; out[7] = t[7];
            out += 8;
        }
        if (!dst) VDP_loadTileData(liteScratch, vram, n, DMA);
        first += n; vram += n; count -= n;
    }
}
#define LOAD_SPRITE_TILES(first, vram, count, tm) \
    lite_tiles(stageArt->spriteTiles, stageArt->spriteIndex, (u16)(first), (vram), (count), (tm))
#define LOAD_PLAYER_TILES(first, vram, count, tm) \
    lite_tiles(stageArt->playerTiles, stageArt->playerIndex, (u16)(first), (vram), (count), (tm))
#define LOAD_BLOCK_TILES(block, vram, count, tm) \
    lite_tiles(jj1ActiveBlockTiles, jj1ActiveBlockIndex, (u16)((block) * JJ1_BLOCK_TILE_COUNT), (vram), (count), (tm))


/* Block uploads during scrolling are queued for vblank so the 68000 is not
 * stalled by a DMA running against the active display; the initial fill of a
 * stage, which is far larger than one frame's queue, goes out immediately. */
static TransferMethod blockTm = DMA;

static void reset_jj1_cache(void)
{
    blockTm = DMA;
    u16 i;
    for (i = 0; i < JJ1_BLOCK_CACHE_BLOCKS; i++) jj1BlockCache[i] = 0xFF;
    for (i = 0; i < 256; i++) jj1BlockSlot[i] = 0xFF;
    jj1VisValid = 0;
    jj1CacheReplace = 0;
    jj1MapOriginX = -1;
    jj1MapOriginY = -1;
}

/* Which source blocks the 11x8 window at the current origin shows, worked
 * out once per window move instead of an 88-cell scan per cache candidate. */
static void mark_visible_window(void)
{
    const u8 *cell = jj1ActiveBlocks + ((u16)jj1MapOriginY << 8) + jj1MapOriginX;
    u8 *const visible = jj1Visible;
    u8 stamp, by, bx;
    if (++jj1VisStamp == 0) {
        memset(jj1Visible, 0, sizeof(jj1Visible));
        jj1VisStamp = 1;
    }
    stamp = jj1VisStamp;
    for (by = 8; by; by--, cell += 256 - 11)
        for (bx = 11; bx; bx--) visible[*cell++] = stamp;
    jj1VisValid = 1;
}

static NO_INLINE u16 cache_jj1_block(u8 sourceBlock)
{
    u16 slot, n;
    if (jj1BlockSlot[sourceBlock] != 0xFF) return jj1BlockSlot[sourceBlock];

    /* Replace only a block which left the 11x8 visible window. This avoids
       rewriting visible VRAM tiles while the plane scrolls.  The search goes
       round from where the last one ended, so it normally stops within a
       slot or two instead of walking over the slots still on screen. */
    if (!jj1VisValid) mark_visible_window();
    slot = jj1CacheReplace;
    for (n = JJ1_BLOCK_CACHE_BLOCKS; n; n--) {
        u8 held = jj1BlockCache[slot];
        if ((held == 0xFF) || (jj1Visible[held] != jj1VisStamp)) break;
        if (++slot == JJ1_BLOCK_CACHE_BLOCKS) slot = 0;
    }
    /* every slot on screen (more than 64 distinct blocks): take the next */
    jj1CacheReplace = (u8)((slot + 1 == JJ1_BLOCK_CACHE_BLOCKS) ? 0 : slot + 1);
    if (jj1BlockCache[slot] != 0xFF) jj1BlockSlot[jj1BlockCache[slot]] = 0xFF;
    jj1BlockCache[slot] = sourceBlock;
    jj1BlockSlot[sourceBlock] = (u8)slot;
    LOAD_BLOCK_TILES(sourceBlock, T_JJ1_BLOCK_CACHE + (slot * JJ1_BLOCK_TILE_COUNT),
        JJ1_BLOCK_TILE_COUNT, blockTm);
    return slot;
}

/* Plane A is built in a RAM copy with the plane's own 64x32 layout and sent
 * by DMA during vblank.  Writing map cells through the data port during the
 * active display (as VDP_setTileMapXY does) runs at the VDP's FIFO speed of
 * a few words per scanline; a scrolled-in column or row cost 3-8 ms. */
#define PLANE_COLS 64
#define PLANE_ROWS 32
static u16 planeShadow[PLANE_ROWS][PLANE_COLS];
static u16 planeColumn[3][4][PLANE_ROWS];   /* tile columns of new block columns */

/* Map word of the first cell of the block at a grid position (a block that
 * has been shot away shows block 0, the backdrop, as the engine's setTile
 * does).  The cache lookup is inline; only a miss calls out. */
#define JJ1_BLOCK_WORD(out, block, bit) do {                                     \
        u8 blk_ = (block);                                                       \
        u8 slot_;                                                                \
        if ((game.destroyed[(bit) >> 3] >> ((bit) & 7)) & 1) blk_ = 0;           \
        slot_ = jj1BlockSlot[blk_];                                              \
        if (slot_ == 0xFF) { PROFMISS(slot_ = (u8)cache_jj1_block(blk_)); }      \
        PROFCOUNT_BLOCK();                                                       \
        (out) = (u16)(TILE_ATTR_FULL(PAL2, FALSE, FALSE, FALSE, T_JJ1_BLOCK_CACHE) + \
                      ((u16)slot_ * JJ1_BLOCK_TILE_COUNT));                      \
    } while (0)
#ifdef JAZZ_PROFDUMP
#define PROFMISS(stmt) do { u32 t0_ = getSubTick(); stmt; profCur[7] += getSubTick() - t0_; } while (0)
#define PROFCOUNT_BLOCK() (profCur[6] += 10)
#else
#define PROFMISS(stmt) stmt
#define PROFCOUNT_BLOCK() ((void)0)
#endif

/* The sixteen map words of a block (w, w+1, ... w+15 in four rows of the
 * RAM copy) as eight long stores: the cells are word-aligned pairs, and the
 * 68000 writes a long in two thirds of the time of two words. */
typedef u32 __attribute__((may_alias)) CellPair;
#define PUT_BLOCK_CELLS(row, w) do {                                             \
        CellPair *p_ = (CellPair *)(row);                                        \
        u32 a_ = ((u32)(w) << 16) | (u16)((w) + 1);                              \
        const u32 two_ = 0x00020002UL;                                           \
        u32 b_ = a_ + two_;                                                      \
        p_[0] = a_; p_[1] = b_; a_ = b_ + two_; b_ = a_ + two_;                  \
        p_[PLANE_COLS / 2] = a_; p_[PLANE_COLS / 2 + 1] = b_; a_ = b_ + two_; b_ = a_ + two_; \
        p_[PLANE_COLS] = a_; p_[PLANE_COLS + 1] = b_; a_ = b_ + two_; b_ = a_ + two_; \
        p_[PLANE_COLS * 3 / 2] = a_; p_[PLANE_COLS * 3 / 2 + 1] = b_;            \
    } while (0)

/* The 4x4 map cells of one 32 px block, into the RAM copy. */
static void draw_jj1_block(s16 sourceX, s16 sourceY)
{
    u16 w, *row, bit;
    u8 ty;
    if ((sourceX < 0) || (sourceX >= 256) || (sourceY < 0) || (sourceY >= 64)) return;
    bit = (u16)((sourceY << 8) + sourceX);
    JJ1_BLOCK_WORD(w, jj1ActiveBlocks[bit], bit);
    row = &planeShadow[(sourceY & 7) * 4][(sourceX & 15) * 4];
    (void)ty;
    PUT_BLOCK_CELLS(row, w);
}

/* Send one block's cells (a block that was just shot away). */
static void send_jj1_block(s16 sourceX, s16 sourceY)
{
    u8 ty;
    u16 py = (u16)((sourceY & 7) * 4), px = (u16)((sourceX & 15) * 4);
    for (ty = 0; ty < 4; ty++)
        DMA_queueDma(DMA_VRAM, &planeShadow[py + ty][px],
                     VDP_getPlaneAddress(BG_A, px, (u16)(py + ty)), 4, 2);
}

/* A new block column (8 blocks: exactly the plane's 32 rows).  The cells go
 * into the RAM copy and into four linear tile-column buffers, which are
 * queued as four DMAs stepping one plane row per word. */
static void build_jj1_column(s16 sourceX, s16 originY, u8 buffer)
{
    const u16 px = (u16)((sourceX & 15) * 4);
    u16 bit = (u16)((originY << 8) + sourceX);
    u16 (*col)[PLANE_ROWS] = planeColumn[buffer];
    u8 by, tx;
    for (by = 8; by; by--, bit = (u16)(bit + 256)) {
        const u16 pr = (u16)(((bit >> 8) & 7) * 4);
        u16 *row = &planeShadow[pr][px];
        u16 w;
        u8 ty;
        JJ1_BLOCK_WORD(w, jj1ActiveBlocks[bit], bit);
        PUT_BLOCK_CELLS(row, w);
        (void)ty;
        {   /* the same cells down each of the four tile columns: w, w+4, w+8, w+12 */
            const u32 four = 0x00040004UL;
            u32 a = ((u32)w << 16) | (u16)(w + 4), b = a + four + four;
            CellPair *c = (CellPair *)&col[0][pr];
            const u32 one = 0x00010001UL;
            c[0] = a; c[1] = b; a += one; b += one;
            c[PLANE_ROWS / 2] = a; c[PLANE_ROWS / 2 + 1] = b; a += one; b += one;
            c[PLANE_ROWS] = a; c[PLANE_ROWS + 1] = b; a += one; b += one;
            c[PLANE_ROWS * 3 / 2] = a; c[PLANE_ROWS * 3 / 2 + 1] = b;
        }
    }
    for (tx = 0; tx < 4; tx++)
        DMA_queueDma(DMA_VRAM, col[tx], VDP_getPlaneAddress(BG_A, (u16)(px + tx), 0),
                     PLANE_ROWS, PLANE_COLS * 2);
}

/* A new block row (11 blocks) into the RAM copy. */
static void build_jj1_row(s16 originX, s16 sourceY)
{
    u16 bit = (u16)((sourceY << 8) + originX);
    u16 (*rows)[PLANE_COLS] = &planeShadow[(sourceY & 7) * 4];
    u8 bx;
    for (bx = 11; bx; bx--, bit++) {
        u16 *cell = &rows[0][(bit & 15) * 4];
        u16 w;
        JJ1_BLOCK_WORD(w, jj1ActiveBlocks[bit], bit);
        PUT_BLOCK_CELLS(cell, w);
    }
}

/* Queue a block row: its four tile rows are contiguous in the plane, so one
 * DMA sends them whole.  The five blocks outside the window are sent as
 * they already are in VRAM (the RAM copy mirrors the plane). */
static void send_jj1_row(s16 sourceY)
{
    const u16 py = (u16)((sourceY & 7) * 4);
    DMA_queueDma(DMA_VRAM, planeShadow[py], VDP_getPlaneAddress(BG_A, 0, py), 4 * PLANE_COLS, 2);
}

static void render_jj1_map(s16 originX, s16 originY)
{
    s16 oldX, oldY, dx, dy, adx, ady, i;
    u16 by;
    if (originX < 0) originX = 0;
    if (originX > (256 - 11)) originX = 256 - 11;
    if (originY < 0) originY = 0;
    if (originY > (64 - 8)) originY = 64 - 8;
    if ((originX == jj1MapOriginX) && (originY == jj1MapOriginY)) return;

    oldX = jj1MapOriginX;
    oldY = jj1MapOriginY;
    jj1MapOriginX = originX;
    jj1MapOriginY = originY;
    jj1VisValid = 0;            /* recomputed on the first cache miss */
    dx = originX - oldX; dy = originY - oldY;
    adx = (dx < 0) ? -dx : dx; ady = (dy < 0) ? -dy : dy;

    if ((oldX < 0) || (adx >= 11) || (ady >= 8)) {
        /* nothing of the old window is left (stage start, a far respawn):
           rebuild the whole plane */
        if (oldX < 0) memset(planeShadow, 0, sizeof(planeShadow));
        for (by = 0; by < 8; by++) build_jj1_row(originX, originY + by);
        DMA_transfer(blockTm, DMA_VRAM, planeShadow, VDP_getPlaneAddress(BG_A, 0, 0),
                     PLANE_ROWS * PLANE_COLS, 2);
        return;
    }
    if (adx + ady > 3) {
        /* a fast pan (the camera catching up after a respawn): build only
           the rows and columns that came into the window, then send the
           plane in one piece */
        s16 bx;
        for (i = 0; i < ady; i++)
            build_jj1_row(originX, (dy > 0) ? (originY + 7 - i) : (originY + i));
        for (i = 0; i < adx; i++) {
            bx = (dx > 0) ? (originX + 10 - i) : (originX + i);
            for (by = 0; by < 8; by++) draw_jj1_block(bx, originY + by);
        }
        DMA_queueDma(DMA_VRAM, planeShadow, VDP_getPlaneAddress(BG_A, 0, 0),
                     PLANE_ROWS * PLANE_COLS, 2);
        return;
    }

    /* Ring-map update: only the new 32px block rows/columns are built (up to
       three in a frame while the camera catches up) and queued.  Rows first:
       a column is copied out for its DMA, so it must be built last to carry
       the cells of a row that changed in the same frame. */
    for (i = 0; i < ady; i++)
        build_jj1_row(originX, (dy > 0) ? (originY + 7 - i) : (originY + i));
    for (i = 0; i < ady; i++)
        send_jj1_row((dy > 0) ? (originY + 7 - i) : (originY + i));
    for (i = 0; i < adx; i++)
        build_jj1_column((dx > 0) ? (originX + 10 - i) : (originX + i), originY, (u8)i);
}

static void render_jj1_sky(void)
{
    static const u8 bands[8] = {0, 1, 2, 4, 6, 7, 7, 7};
    u16 x, y;
    VDP_clearPlane(BG_B, TRUE);
    for (y = 0; y < 32; y++) {
        u16 tile = T_JJ1_SKY + bands[(y >> 2) & 7];
        for (x = 0; x < 64; x++)
            VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL3, FALSE, FALSE, FALSE, tile), x, y);
    }
}

/* One track per world, from the mapped episode pack.  The XGM driver loops
 * the track until the next call, so playback only restarts when the track
 * actually changes. */
static const u8 *stage_music(u8 stage)
{
    if (stage >= JJ1_PACK_STAGES) stage = 0;
    return JJ1_GAME_PACK->music[stage];
}

static const u8 *playingMusic;

static void play_music(const u8 *track)
{
    if (track == playingMusic) return;   /* already playing this one */
    playingMusic = track;
    jj1_music_start(track);
}

/* Level palette animation: each reserved slot group turns through its ring of
 * colours, in the direction and at the pace the original engine uses (see
 * tools/jj1_cycle.py).  Phases restart with every stage. */
static u8 cyclePhase[4], cycleTick[4];

static void reset_palette_cycles(void)
{
    for (u8 i = 0; i < 4; i++) { cyclePhase[i] = 0; cycleTick[i] = 0; }
}

/* Rotates the rings that are due.  In play the colours are queued for vblank
 * (a CRAM write during the active display shows as dots on real hardware and
 * changed the lower part of the picture a frame early); the self-test asks
 * for immediate writes so it can read them back. */
static void update_palette_cycles(u8 immediate)
{
    static u16 ring[4][4];
    for (u8 i = 0; i < stageArt->cycleCount; i++) {
        const Jj1Cycle *c = &stageArt->cycles[i];
        if (!(stageArt->cycleMask & c->bit)) continue;
        if (++cycleTick[i] < c->stepFrames) continue;
        cycleTick[i] = 0;
        u8 k = c->count, p;
        cyclePhase[i] = (c->dir > 0) ? (u8)(cyclePhase[i] ? cyclePhase[i] - 1 : k - 1)
                                     : (u8)((cyclePhase[i] + 1 == k) ? 0 : cyclePhase[i] + 1);
        p = cyclePhase[i];
        for (u8 s = 0; s < k; s++) {
            ring[i][s] = c->colors[p];
            if (++p == k) p = 0;
        }
        PAL_setColors((PAL2 * 16) + c->slot, ring[i], k, immediate ? CPU : DMA_QUEUE);
    }
}

static void render_backdrop(u8 stage)
{
    /* Everything that varies by stage comes from one table: the world's tile
       bank and palette, plus this level's sprite palette, Jazz frames, springs
       and event sprites. */
    stageArt = &jazz_stage_art[(stage < JAZZ_STAGE_COUNT) ? stage : 0];
    jj1ActiveBlockTiles = stageArt->blockTiles;
    jj1ActiveBlockIndex = stageArt->blockIndex;
    jj1ActiveBlocks = jj1_runtime_blocks(stage);

    PAL_setPalette(PAL2, stageArt->blockPalette, DMA);
    reset_palette_cycles();
    PAL_setPalette(PAL1, stageArt->pal1, DMA);
    VDP_loadTileData(stageArt->springTiles, T_SPRING,
                     JJ1_SPRING_VARIANTS * JJ1_SPRING_TILES_PER_VARIANT, DMA);
    /* Force the streamed slots to refill against the new art. */
    playerSlotFrame = 0xFFFF;
    boardSlotKey = 0xFFFF;
    bossGunArt = 0xFFFF;
    itemSlotLoaded[0] = itemSlotLoaded[1] = 0xFFFF;
    for (u8 i = 0; i < JAZZ_MAX_ENEMIES; i++) enemySlotArt[i] = 0xFFFF;

    render_jj1_sky();
    reset_jj1_cache();
    render_jj1_map(0, 0);
    VDP_setHorizontalScroll(BG_A, 0);
    VDP_setHorizontalScroll(BG_B, 0);

    /* Start the world's track (Diamondus / Tubelectric / Medivo). */
    play_music(stage_music(stage));
}

static void load_video_assets(void)
{
    VDP_setScreenWidth320();
    VDP_setPlaneSize(64, 32, TRUE);
    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_loadTileData(jazz_world_tiles, T_EMPTY, 7, DMA);
    /* Jazz streams like the enemies: only the current frame is resident. */
    VDP_loadTileData(jazz_enemy_walker_tiles, T_ENEMY_WALK, 2 * JJ1_WALKER_FRAME_TILES, DMA);
    VDP_loadTileData(jazz_enemy_flyer_tiles, T_ENEMY_FLY, 2 * JJ1_FLYER_FRAME_TILES, DMA);
    VDP_loadTileData(jazz_enemy_hazard_tiles, T_ENEMY_HAZARD, 4, DMA);
    VDP_loadTileData(jazz_gem_tile, T_GEM, 1, DMA);
    {
        /* A solid tile for the guardian's energy bar. */
        static const u32 solid[8] = {
            0x33333333, 0x33333333, 0x33333333, 0x33333333,
            0x33333333, 0x33333333, 0x33333333, 0x33333333
        };
        VDP_loadTileData(solid, T_BOSSBAR, 1, DMA);
    }
    VDP_loadTileData(jazz_bullet_tile, T_BULLET, 1, DMA);
    VDP_loadTileData(jj1_sky_tiles, T_JJ1_SKY, 8, DMA);
    /* The game's own FONT2.0FN glyphs and their palette. */
    VDP_loadFont(&jj1_font, DMA);
    PAL_setPalette(PAL0, jj1_font_pal.data, DMA);
    PAL_setPalette(PAL3, jj1_sky_palette, DMA);
    VDP_setBackgroundColor(0);
    VDP_setTextPalette(PAL0);
    VDP_setTextPriority(TRUE);
    PSG_reset();

    /* Prime the XGM driver's sample table so the effects can play over the
       music.  IDs 64+ are the user range; channel 1 is left to the music. */
    XGM_setPCM(JJ1_SFX_FIRE, jj1_fire, sizeof(jj1_fire));
    XGM_setPCM(JJ1_SFX_JUMP, jj1_jump, sizeof(jj1_jump));
    XGM_setPCM(JJ1_SFX_PICKUP, jj1_pickup, sizeof(jj1_pickup));
    XGM_setPCM(JJ1_SFX_BOOM, jj1_boom, sizeof(jj1_boom));
    XGM_setPCM(JJ1_SFX_OW, jj1_ow, sizeof(jj1_ow));
    XGM_setPCM(JJ1_SFX_ONEUP, jj1_oneup, sizeof(jj1_oneup));
    XGM_setPCM(JJ1_SFX_YEAHOO, jj1_yeahoo, sizeof(jj1_yeahoo));
    XGM_setPCM(JJ1_SFX_FLAME, jj1_flame, sizeof(jj1_flame));
    XGM_setPCM(JJ1_SFX_PHOTON, jj1_photon, sizeof(jj1_photon));
    XGM_setPCM(JJ1_SFX_SPRING, jj1_spring, sizeof(jj1_spring));
    XGM_setPCM(JJ1_SFX_INVULN, jj1_invuln, sizeof(jj1_invuln));
    XGM_setPCM(JJ1_SFX_BIRDY, jj1_birdy, sizeof(jj1_birdy));
    XGM_setPCM(JJ1_SFX_ROCKET, jj1_rocket, sizeof(jj1_rocket));
    XGM_setPCM(JJ1_SFX_GODLIKE, jj1_godlike, sizeof(jj1_godlike));
    XGM_setPCM(JJ1_SFX_BLOCK3, jj1_block3, sizeof(jj1_block3));
    XGM_setPCM(JJ1_SFX_EVT_MACHGUN, jj1_evt_machgun, sizeof(jj1_evt_machgun));
    XGM_setPCM(JJ1_SFX_EVT_FIRE1, jj1_evt_fire1, sizeof(jj1_evt_fire1));
    XGM_setPCM(JJ1_SFX_EVT_UPLOOP, jj1_evt_uploop, sizeof(jj1_evt_uploop));
    XGM_setPCM(JJ1_SFX_EVT_PHOTON, jj1_evt_photon, sizeof(jj1_evt_photon));
    XGM_setPCM(JJ1_SFX_EVT_WAIT, jj1_evt_wait, sizeof(jj1_evt_wait));
    XGM_setPCM(JJ1_SFX_EVT_ORB, jj1_evt_orb, sizeof(jj1_evt_orb));
    XGM_setPCM(JJ1_SFX_EVT_FLAMER, jj1_evt_flamer, sizeof(jj1_evt_flamer));
    XGM_setPCM(JJ1_SFX_EVT_ELECTR, jj1_evt_electr, sizeof(jj1_evt_electr));
    XGM_setPCM(JJ1_SFX_EVT_ROCKET2, jj1_evt_rocket2, sizeof(jj1_evt_rocket2));
    XGM_setPCM(JJ1_SFX_EVT_STOP, jj1_evt_stop, sizeof(jj1_evt_stop));
    XGM_setPCM(JJ1_SFX_EVT_BLOCK1, jj1_evt_block1, sizeof(jj1_evt_block1));
}

/* Follow the player with lag proportional to the distance, as the original
 * does (it lerps the viewport toward the player by a fraction each tick).
 * The previous code moved the camera by at most 3 px per frame, but Jazz runs
 * at over 5 px per frame: the camera could never keep up, so it fell behind
 * and then lurched forward, which read as jittery back-and-forth scrolling.
 * Vertically it was worse - a 45-degree mask ramp moves the player 4-6 px per
 * frame, so the capped camera climbed in visible chunks and made smooth slopes
 * look like stairs.  A proportional step has no speed ceiling: at a constant
 * player speed it settles into a constant lag and then tracks exactly. */
static s16 camera_follow(s16 camera, s16 wanted)
{
    s16 delta = wanted - camera;
    s16 step;
    if (!delta) return camera;
    step = delta >> 2;
    if (!step) step = (delta > 0) ? 1 : -1;  /* always converge the last few px */
    return (s16)(camera + step);
}

#ifdef JAZZ_MAPCHECK
/* Test builds: every visible 32 px cell of plane A must show the cached copy
 * of the block the level puts there (shot-away blocks show block 0). */
u16 mapCheckBad, mapCheckRuns, mapCheckBig, mapCheckBadSmall;
static void map_check(void)
{
    static u8 seen[256];
    u16 distinct = 0, before = mapCheckBad;
    {
        u16 i, x, y;
        for (i = 0; i < 256; i++) seen[i] = 0;
        for (y = 0; y < 8; y++) for (x = 0; x < 11; x++) {
            s16 sx = jj1MapOriginX + x, sy = jj1MapOriginY + y;
            u8 b = jj1ActiveBlocks[(sy * 256) + sx];
            if (jazz_cell_destroyed(&game, (u8)sx, (u8)sy)) b = 0;
            if (!seen[b]) { seen[b] = 1; distinct++; }
        }
    }
    vu32 *ctrl = (vu32 *)VDP_CTRL_PORT;
    vu16 *data = (vu16 *)VDP_DATA_PORT;
    u16 bx, by, ty, tx;
    mapCheckRuns++;
    for (by = 0; by < 8; by++)
        for (bx = 0; bx < 11; bx++) {
            s16 sx = jj1MapOriginX + bx, sy = jj1MapOriginY + by;
            u8 want = jj1ActiveBlocks[(sy * 256) + sx];
            u16 slot;
            if (jazz_cell_destroyed(&game, (u8)sx, (u8)sy)) want = 0;
            slot = jj1BlockSlot[want];
            if ((slot == 0xFF) || (jj1BlockCache[slot] != want)) { mapCheckBad++; continue; }
            for (ty = 0; ty < 4; ty++) {
                u16 addr = VDP_getPlaneAddress(BG_A, (u16)((sx & 15) * 4), (u16)((sy & 7) * 4 + ty));
                SYS_disableInts();
                *ctrl = VDP_READ_VRAM_ADDR((u32)addr);
                for (tx = 0; tx < 4; tx++) {
                    u16 w = *data;
                    if ((w & 0x7FF) != T_JJ1_BLOCK_CACHE + slot * JJ1_BLOCK_TILE_COUNT + ty * 4 + tx)
                        mapCheckBad++;
                }
                SYS_enableInts();
            }
        }
    if (distinct > 64) mapCheckBig++;
    else if (mapCheckBad != before) mapCheckBadSmall++;
}
#endif

static void update_camera_and_map(void)
{
    s16 wantedX = game.player.x - 150;
    s16 wantedY = game.player.y - 96;
    if (wantedX < 0) wantedX = 0;
    if (wantedY < 0) wantedY = 0;
    if (wantedX > ((256 * 32) - SCREEN_W)) wantedX = (256 * 32) - SCREEN_W;
    if (wantedY > ((64 * 32) - 224)) wantedY = (64 * 32) - 224;
    cameraX = camera_follow(cameraX, wantedX);
    cameraY = camera_follow(cameraY, wantedY);
    render_jj1_map(cameraX >> 5, cameraY >> 5);
#ifdef JAZZ_MAPCHECK
    {
        char t[12];
        intToStr(mapCheckBadSmall, t, 4); VDP_drawTextBG(WINDOW, t, 34, 1);
        intToStr(mapCheckBig, t, 4); VDP_drawTextBG(WINDOW, t, 29, 1);
        intToStr(mapCheckRuns, t, 5); VDP_drawTextBG(WINDOW, t, 23, 1);
    }
#endif
    /* Applied during vblank, with the map DMAs and the sprite table, so the
       scroll, the new map cells and the sprites change on the same frame. */
    VDP_setHorizontalScrollVSync(BG_A, -cameraX);
    VDP_setVerticalScrollVSync(BG_A, cameraY);
    VDP_setHorizontalScroll(BG_B, 0);
}

/* One hardware sprite, straight into SGDK's sprite table copy (the same
 * fields VDP_setSpriteFull sets, without a call per sprite). */
static inline void put_sprite(u16 *count, s16 x, s16 y, u8 size, u16 attr)
{
    const u16 n = *count;
    if ((n < 79) && (x > -40) && (x < 336) && (y > -40) && (y < 240)) {
        VDPSprite *spr = &vdpSpriteCache[n];
        spr->y = (s16)(y + 0x80);
        spr->size = size;
        spr->link = (u8)(n + 1);
        spr->attribut = attr;
        spr->x = (s16)(x + 0x80);
        *count = (u16)(n + 1);
    }
}

/* Which event id each item VRAM slot currently holds; 0xFF = free. */
static u8 itemSlotEvent[JJ1_ITEM_SLOTS];
/* What each item slot's VRAM actually holds (event id and animation frame),
 * kept across frames so a pickup that is still on screen is not re-uploaded
 * sixty times a second; 0xFFFF = unknown. */

static void upload_item_slot(u8 slot, u16 key, u16 first, u16 tiles)
{
    if (itemSlotLoaded[slot] == key) return;
    LOAD_SPRITE_TILES(first, T_ITEM_BASE + (slot * JJ1_ITEM_SLOT_TILES), tiles, DMA_QUEUE);
    itemSlotLoaded[slot] = key;
}

/* Springs and uncollected items are drawn straight from the original event
 * grid within the visible 11x8 block window; the taken bitmap hides
 * collected cells.  Items use their real extracted sprite, streamed into a
 * small pool keyed by event id so a run of identical pickups shares one slot. */
/* The guardian's energy bar.  The original draws a vertical column near the
 * right edge whose height falls from 100 px to nothing as hits accumulate, so
 * the fight has visible progress. */
static void draw_boss_energy(u16 *count, const JazzEnemy *e)
{
    const Jj1EventInfo *bi =
        jj1_event_info(game.stage, jj1_runtime_event(game.stage, e->gridX, e->gridY));
    u8 total = bi->strength ? bi->strength : 1;
    u8 spent = (u8)((total > e->hitPoints) ? (total - e->hitPoints) : 0);
    s16 lost = (s16)(((s16)spent * 100) / total);   /* percent gone */
    s16 top = (s16)(40 + lost);                      /* bar shrinks downward */
    s16 barX = 320 - 40;
    s16 y = top;
    /* Only one solid tile is reserved here, so the column is built from 1x1
       sprites.  Asking for a taller or wider sprite makes the VDP fetch the
       tiles that follow this one - the sky - and the bar renders as garbage
       that never visibly changes. */
    while (y < 140) {
        put_sprite(count, barX, y, SPRITE_SIZE(1, 1),
                   TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, T_BOSSBAR));
        y = (s16)(y + 8);
    }
}

/* What an event id draws with this frame: the item slot it holds, or
 * EV_NO_SLOT (its art is missing or too big, or both slots were taken).  The
 * answer cannot change within a frame - slots are only ever claimed - so it
 * is worked out at the id's first cell and remembered for the rest. */
#define EV_NO_SLOT 0xFE
static u8 evMemo[128], evMemoStamp[128], evStamp;
static struct { s16 ox, oy; u16 attr; u8 size; } evSlotDraw[JJ1_ITEM_SLOTS];

static NO_INLINE u8 event_slot(u8 id, u8 bridge)
{
    const Jj1EventSprite *art = stageArt->sprites ? &stageArt->sprites[id] : 0;
    u8 slot = 0xFF, s;
    evMemoStamp[id] = evStamp;
    evMemo[id] = EV_NO_SLOT;
    if (!art || !art->frames || ((u16)(art->tilesW * art->tilesH) > JJ1_ITEM_SLOT_TILES))
        return EV_NO_SLOT;
    for (s = 0; s < JJ1_ITEM_SLOTS; s++)
        if (itemSlotEvent[s] == id) { slot = s; break; }
    if (slot == 0xFF)
        for (s = 0; s < JJ1_ITEM_SLOTS; s++)
            if (itemSlotEvent[s] == 0xFF) {
                slot = s;
                itemSlotEvent[s] = id;
                if (bridge) {
                    upload_item_slot(s, (u16)(0x8000 | id), art->tile,
                        (u16)(art->tilesW * art->tilesH));
                } else {
                    /* The whole cell is uploaded, so the column-major tile
                       order stays intact. */
                    u8 frame = anim_frame(art->frames);
                    upload_item_slot(s, (u16)((id << 4) | (frame & 15)),
                        (u16)(art->tile + mul_u8(frame, (u8)mul_u8(art->tilesW, art->tilesH))),
                        (u16)(art->tilesW * art->tilesH));
                }
                break;
            }
    if (slot == 0xFF) return EV_NO_SLOT;
    evSlotDraw[slot].ox = (s16)((32 - (s16)(art->tilesW << 3)) >> 1);
    evSlotDraw[slot].oy = (s16)((32 - (s16)(art->tilesH << 3)) >> 1);
    evSlotDraw[slot].size = SPRITE_SIZE(art->tilesW, art->tilesH);
    evSlotDraw[slot].attr = TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE,
                                           T_ITEM_BASE + (slot * JJ1_ITEM_SLOT_TILES));
    evMemo[id] = slot;
    return slot;
}

static void render_jj1_events(u16 *count)
{
    const u8 *grid = jj1_runtime_events(game.stage);
    const u8 *klassOf = jj1_runtime_klass_table(game.stage);
    const Jj1EventInfo *infos = jj1_event_info(game.stage, 0);
    const u8 *cell;
    s16 py;
    u16 bit;
    u8 i, by;
    for (i = 0; i < JJ1_ITEM_SLOTS; i++) itemSlotEvent[i] = 0xFF;
    if (++evStamp == 0) {
        memset(evMemoStamp, 0, sizeof(evMemoStamp));
        evStamp = 1;
    }
    if ((jj1MapOriginX < 0) || (jj1MapOriginY < 0)) return;
    /* the 11x8 block window of the map (always inside the level) */
    py = (s16)((jj1MapOriginY << 5) - cameraY);
    cell = grid + ((u16)jj1MapOriginY << 8) + jj1MapOriginX;
    for (by = 8; by; by--, py = (s16)(py + 32), cell += 256 - 11) {
        s16 px = (s16)((jj1MapOriginX << 5) - cameraX - 32);
        u8 bx;
        for (bx = 11; bx; bx--) {
            const u8 id = *cell++;
            u8 klass;
            px = (s16)(px + 32);
            if (!id) continue;                     /* most cells hold nothing */
            if (id >= 128) continue;
            klass = klassOf[id];
            bit = (u16)(cell - 1 - grid);
            if (klass == JJ1_CLASS_ITEM) {
                u8 slot;
                if ((game.taken[bit >> 3] >> (bit & 7)) & 1) continue;
                slot = (evMemoStamp[id] == evStamp) ? evMemo[id] : event_slot(id, 0);
                if (slot != EV_NO_SLOT)
                    put_sprite(count, (s16)(px + evSlotDraw[slot].ox), (s16)(py + evSlotDraw[slot].oy),
                               evSlotDraw[slot].size, evSlotDraw[slot].attr);
                else
                    /* No sprite, one too large for a slot, or more distinct
                       item types on screen than slots: the gem dot. */
                    put_sprite(count, (s16)(px + 12), (s16)(py + 12),
                        SPRITE_SIZE(1, 1), TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, T_GEM));
            } else if (klass == JJ1_CLASS_SPRING) {
                /* param carries the launch magnitude; bucket it into the
                   three visual strengths. */
                const u8 param = infos[id].param;
                u16 variant = (param >= 12) ? 2 : ((param >= 9) ? 1 : 0);
                put_sprite(count, px, (s16)(py + 16),
                    SPRITE_SIZE(4, 2), TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE,
                    T_SPRING + (variant * JJ1_SPRING_TILES_PER_VARIANT)));
            } else if (klass == JJ1_CLASS_BRIDGE) {
                /* One bridge event is a whole span: `param` logs spaced
                   `strength` px apart, with the deck `points` px below the cell
                   top.  The span is drawn from its own cell so it is emitted
                   once, not once per column it crosses. */
                const Jj1EventInfo *info = &infos[id];
                u8 slot = (evMemoStamp[id] == evStamp) ? evMemo[id] : event_slot(id, 1);
                if (slot != EV_NO_SLOT) {
                    s16 deckY = (s16)(py + info->points);
                    s16 lx = px;
                    u8 piece;
                    for (piece = 0; piece < info->param; piece++, lx = (s16)(lx + info->strength)) {
                        if ((lx < -32) || (lx > 320)) continue;  /* off screen */
                        put_sprite(count, lx, deckY, evSlotDraw[slot].size, evSlotDraw[slot].attr);
                    }
                }
            }
        }
    }
}

static void render_sprites(void)
{
    u16 count = 0;
    u8 i;
#ifdef JAZZ_PROFILE
    u32 pt0 = getSubTick();
#endif
    VDP_clearSprites();     /* (the sprite allocator SGDK also resets is not used) */
    {
        /* Jazz's animation state comes from the shared physics; each state is
           a cell (up to 2x2 chunks) streamed into one VRAM slot, refilled when
           the state or frame changes. */
        u8 state = jazz_player_anim_state(&game, lastInput);
        const Jj1EventSprite *art = &stageArt->playerStates[state];
        u8 frames = art->frames ? art->frames : 1;
        u8 frame = anim_frame(frames);
        u16 wanted = (u16)((state << 3) | frame);
        u16 cellW = (u16)(art->tilesW << 3);
        u16 cellH = (u16)(art->tilesH << 3);
        u8 flip = !game.player.facing;
        if (playerSlotFrame != wanted) {
            LOAD_PLAYER_TILES((art->tile + mul_u8(frame, (u8)mul_u8(art->tilesW, art->tilesH))),
                             T_PLAYER, mul_u8(art->tilesW, art->tilesH), DMA_QUEUE);
            playerSlotFrame = wanted;
        }
        /* Airboard: the original draws the board sprite 10 px below the
           player origin while flying, under the riding pose. */
        if (game.player.flying && stageArt->board && stageArt->board->frames) {
            const Jj1EventSprite *bd =
                game.player.facing ? stageArt->board : stageArt->boardLeft;
            u16 need = (u16)(bd->tilesW * bd->tilesH);
            if (need <= JJ1_BOARD_SLOT_TILES) {
                u8 frame = anim_frame(bd->frames);
                u16 wanted = (u16)(0x200 | (game.player.facing << 4) | frame);
                if (boardSlotKey != wanted) {
                    LOAD_SPRITE_TILES((bd->tile + (u16)mul_u8(frame, (u8)need)), T_BOARD, need, DMA_QUEUE);
                    boardSlotKey = wanted;
                }
                put_sprite(&count,
                           game.player.x - cameraX + (PLAYER_W >> 1) - (s16)(bd->tilesW << 2),
                           game.player.y - cameraY + 10,
                           SPRITE_SIZE(bd->tilesW, bd->tilesH),
                           TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, T_BOARD));
            }
        }

        /* Bird companion and shield orb share the second item slot, which is
           free while they are on screen (both are small, single-cell art). */
        if (game.player.bird && stageArt->bird && stageArt->bird->frames) {
            const Jj1EventSprite *b =
                game.player.facing ? stageArt->bird : stageArt->birdLeft;
            u16 need = (u16)(b->tilesW * b->tilesH);
            if (need <= JJ1_ITEM_SLOT_TILES) {
                u8 frame = anim_frame(b->frames);
                u16 key = (u16)(0xB0 | frame);
                u16 slot = T_ITEM_BASE + ((JJ1_ITEM_SLOTS - 1) * JJ1_ITEM_SLOT_TILES);
                if (itemSlotEvent[JJ1_ITEM_SLOTS - 1] != key) {
                    LOAD_SPRITE_TILES((b->tile + (u16)mul_u8(frame, (u8)need)), slot, need, DMA_QUEUE);
                    itemSlotEvent[JJ1_ITEM_SLOTS - 1] = (u8)key;
                    itemSlotLoaded[JJ1_ITEM_SLOTS - 1] = 0xFFFF;
                }
                put_sprite(&count, game.player.birdX - cameraX,
                           game.player.birdY - cameraY,
                           SPRITE_SIZE(b->tilesW, b->tilesH),
                           TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, slot));
            }
        }

        /* Shield: orbiting orbs, one per remaining hit, as the original draws
           LA_4SHIELD/LA_1SHIELD circling the player at radius 20. */
        if (game.player.shield && stageArt->shield && stageArt->shield->frames) {
            static const s8 ring[8][2] = {
                { 20, 0 }, { 14, 14 }, { 0, 20 }, { -14, 14 },
                { -20, 0 }, { -14, -14 }, { 0, -20 }, { 14, -14 }
            };
            const Jj1EventSprite *sh = stageArt->shield;
            u16 need = (u16)(sh->tilesW * sh->tilesH);
            if (need <= JJ1_ITEM_SLOT_TILES) {
                u16 slot = T_ITEM_BASE;
                u8 phase = (u8)((game.frame >> 1) & 7);
                u8 orbs = (game.player.shield > 4) ? 4 : game.player.shield;
                u8 k;
                if (itemSlotEvent[0] != 0xFE) {
                    LOAD_SPRITE_TILES(sh->tile,
                                     slot, need, DMA_QUEUE);
                    itemSlotEvent[0] = 0xFE;
                    itemSlotLoaded[0] = 0xFFFF;
                }
                for (k = 0; k < orbs; k++) {
                    const s8 *o = ring[(phase + (k << 1)) & 7];
                    put_sprite(&count,
                               game.player.x - cameraX + (PLAYER_W >> 1) - 4 + o[0],
                               game.player.y - cameraY + (PLAYER_H >> 1) - 4 + o[1],
                               SPRITE_SIZE(sh->tilesW, sh->tilesH),
                               TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, slot));
                }
            }
        }

        /* Invincibility sparks: four sparkles orbiting the player, as the
           original draws with fSin/fCos at radius 12.  A small quarter-circle
           table avoids trig on the 68000. */
        if (game.player.invincibleTime) {
            static const s8 orbit[8][2] = {
                { 12, 0 }, { 8, 8 }, { 0, 12 }, { -8, 8 },
                { -12, 0 }, { -8, -8 }, { 0, -12 }, { 8, -8 }
            };
            u8 phase = (u8)((game.frame >> 1) & 7);
            u8 k;
            for (k = 0; k < 4; k++) {
                const s8 *o = orbit[(phase + (k << 1)) & 7];
                put_sprite(&count,
                           game.player.x - cameraX + (PLAYER_W >> 1) - 4 + o[0],
                           game.player.y - cameraY + (PLAYER_H >> 1) - 4 + o[1],
                           SPRITE_SIZE(1, 1),
                           TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, T_GEM));
            }
        }
        if (!(game.invulnerability && (game.frame & 4))) {
            u16 tile = T_PLAYER;
            u8 cx, cy;
            for (cy = 0; cy < art->tilesH; cy += 4) {
                u8 ch = (u8)((art->tilesH - cy > 4) ? 4 : (art->tilesH - cy));
                for (cx = 0; cx < art->tilesW; cx += 4) {
                    u8 cw = (u8)((art->tilesW - cx > 4) ? 4 : (art->tilesW - cx));
                    s16 ox = (s16)(cx << 3);
                    if (flip) ox = (s16)(cellW - ox - (cw << 3));
                    /* Centre horizontally on the body; stand feet on the floor. */
                    put_sprite(&count,
                               game.player.x - cameraX + (PLAYER_W >> 1) - (s16)(cellW >> 1) + ox,
                               game.player.y - cameraY + PLAYER_H - (s16)cellH + (s16)(cy << 3),
                               SPRITE_SIZE(cw, ch),
                               TILE_ATTR_FULL(PAL1, TRUE, FALSE, flip, tile));
                    tile = (u16)(tile + mul_u8(cw, ch));
                }
            }
        }
    }
#ifdef JAZZ_PROFILE
    profT[7] += getSubTick() - pt0;
#endif
    PROF(5, render_jj1_events(&count));
#ifdef JAZZ_PROFILE
    pt0 = getSubTick();
#endif
    for (i = 0; i < JAZZ_MAX_ENEMIES; i++)
        if (game.enemies[i].active) {
            const JazzEnemy *e = &game.enemies[i];
            /* Off-screen enemies cost nothing: no event lookup, no VRAM
               upload, no sprite entries.  (The guardian is large and carries
               an energy bar, so it is always processed.) */
            if (e->klass != JJ1_CLASS_BOSS) {
                s16 ex = (s16)(e->x - cameraX), ey = (s16)(e->y - cameraY);
                if ((ex < -80) || (ex > 400) || (ey < -80) || (ey > 300)) continue;
            }
            /* The guardian's facing comes from its circuit state (1 and 2
               sweep left, 3 and 4 right); ordinary enemies use the sign. */
            u8 facingLeft = (e->klass == JJ1_CLASS_BOSS) ? e->bossFacing
                                                         : (e->direction < 0);
            /* Runtime check, not #ifdef: the generated include is compiled
               into gfx.c, so this translation unit never sees its macro.  gfx.c
               exports a null pointer when no original sprites were extracted. */
            u8 id = jj1_runtime_event(game.stage, e->gridX, e->gridY);
            const Jj1EventSprite *art = stageArt->sprites ? &stageArt->sprites[id] : 0;
            u8 bossCharging = 0;
            if (e->klass == JJ1_CLASS_BOSS) {
                /* Past half health the guardian becomes the drill, body and
                   accessory both. */
                const Jj1EventInfo *bi = jj1_event_info(game.stage, id);
                u8 total = bi->strength ? bi->strength : 2;
                bossCharging = (u8)((total - e->hitPoints) >= (total >> 1));
                if (bossCharging && stageArt->bossLate && stageArt->bossLate->frames)
                    art = facingLeft ? stageArt->bossLateLeft : stageArt->bossLate;
            }
            if (art && art->frames) {
                /* One VRAM slot per active enemy: upload the current frame only
                   when it changes, so a walking enemy costs one DMA every few
                   frames rather than one every frame.  JJ1 draws pre-made art
                   per direction, so pick the left-facing set when moving left
                   (tileLeft == tile when the enemy has no distinct left art),
                   and never hardware-flip - that would double-mirror the many
                   enemies whose "right" art is itself drawn facing left. */
                u16 slot = T_ENEMY_BASE + (i * JJ1_ENEMY_SLOT_TILES);
                u8 frame = anim_frame(art->frames);
                u16 srcTile = (facingLeft ? art->tileLeft : art->tile);
                u16 wanted = (u16)((srcTile << 3) | frame);
                u16 cellW = (u16)(art->tilesW << 3);
                u16 cellH = (u16)(art->tilesH << 3);
                u16 tile = slot;
                u8 cx, cy;
                if (enemySlotArt[i] != wanted) {
#ifndef NOENEMYDMA
                    LOAD_SPRITE_TILES((srcTile + mul_u8(frame, (u8)mul_u8(art->tilesW, art->tilesH))),
                                     slot, mul_u8(art->tilesW, art->tilesH), DMA_QUEUE);
#endif
                    enemySlotArt[i] = wanted;
                }
                /* The cell is centred on the body and stands on its feet. */
                for (cy = 0; cy < art->tilesH; cy += 4) {
                    u8 ch = (u8)((art->tilesH - cy > 4) ? 4 : (art->tilesH - cy));
                    for (cx = 0; cx < art->tilesW; cx += 4) {
                        u8 cw = (u8)((art->tilesW - cx > 4) ? 4 : (art->tilesW - cx));
                        s16 ox = (s16)(cx << 3);
                        put_sprite(&count,
                                   e->x - cameraX + (JAZZ_ENEMY_W >> 1) - (s16)(cellW >> 1) + ox,
                                   e->y - cameraY + JAZZ_ENEMY_H - (s16)cellH + (s16)(cy << 3),
                                   SPRITE_SIZE(cw, ch),
                                   TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, tile));
                        tile = (u16)(tile + mul_u8(cw, ch));
                    }
                }
                /* The guardian carries an accessory - the cannon while it
                   circles, the drill spike once it charges - positioned from
                   the body sprite's own top-left corner, which is where the
                   emitted delta is measured from.  Anchoring it to the
                   collision origin instead is what left the gun adrift. */
                if (e->klass == JJ1_CLASS_BOSS) {
                    const Jj1EventSprite *gun = bossCharging
                        ? (facingLeft ? stageArt->bossSpikeLeft : stageArt->bossSpike)
                        : (facingLeft ? stageArt->bossCannonLeft : stageArt->bossCannon);
                    s16 gx = bossCharging
                        ? (facingLeft ? stageArt->bossSpikeLeftX : stageArt->bossSpikeX)
                        : (facingLeft ? stageArt->bossCannonLeftX : stageArt->bossCannonX);
                    s16 gy = bossCharging
                        ? (facingLeft ? stageArt->bossSpikeLeftY : stageArt->bossSpikeY)
                        : (facingLeft ? stageArt->bossCannonLeftY : stageArt->bossCannonY);
                    if (gun && gun->frames) {
                        u16 gunTiles = (u16)(gun->tilesW * gun->tilesH);
                        u16 gunSlot = (u16)(slot + (art->tilesW * art->tilesH));
                        u8 gframe = anim_frame(gun->frames);
                        u16 gwant = (u16)(0x4000 | (gun->tile << 2) | gframe);
                        s16 cellX = e->x - cameraX + (JAZZ_ENEMY_W >> 1) - (s16)(cellW >> 1);
                        s16 cellY = e->y - cameraY + JAZZ_ENEMY_H - (s16)cellH;
                        if (bossGunArt != gwant) {
                            LOAD_SPRITE_TILES((gun->tile + (u16)mul_u8(gframe, (u8)gunTiles)),
                                gunSlot, gunTiles, DMA_QUEUE);
                            bossGunArt = gwant;
                        }
                        {
                            u16 gt = gunSlot;
                            u8 kx, ky;
                            for (ky = 0; ky < gun->tilesH; ky += 4) {
                                u8 kh = (u8)((gun->tilesH - ky > 4) ? 4 : (gun->tilesH - ky));
                                for (kx = 0; kx < gun->tilesW; kx += 4) {
                                    u8 kw = (u8)((gun->tilesW - kx > 4) ? 4 : (gun->tilesW - kx));
                                    put_sprite(&count,
                                        cellX + gx + (s16)(kx << 3),
                                        cellY + gy + (s16)(ky << 3),
                                        SPRITE_SIZE(kw, kh),
                                        TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, gt));
                                    gt = (u16)(gt + mul_u8(kw, kh));
                                }
                            }
                        }
                    }
                    draw_boss_energy(&count, e);
                }
                continue;
            }
            if (e->klass == JJ1_CLASS_ENEMY_WALK)
                put_sprite(&count, e->x - cameraX - 9, e->y - cameraY,
                    SPRITE_SIZE(4, 2), TILE_ATTR_FULL(PAL1, TRUE, FALSE, facingLeft,
                    T_ENEMY_WALK + (((game.frame >> 3) & 1) * JJ1_WALKER_FRAME_TILES)));
            else if (e->klass == JJ1_CLASS_ENEMY_FLY)
                put_sprite(&count, e->x - cameraX - 1, e->y - cameraY,
                    SPRITE_SIZE(2, 2), TILE_ATTR_FULL(PAL1, TRUE, FALSE, facingLeft,
                    T_ENEMY_FLY + (((game.frame >> 3) & 1) * JJ1_FLYER_FRAME_TILES)));
            else
                put_sprite(&count, e->x - cameraX - 1, e->y - cameraY,
                    SPRITE_SIZE(2, 2), TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE,
                    T_ENEMY_HAZARD));
        }
    for (i = 0; i < JAZZ_MAX_BULLETS; i++)
        if (game.bullets[i].active)
            put_sprite(&count, game.bullets[i].x - cameraX, game.bullets[i].y - cameraY, SPRITE_SIZE(1, 1), TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, T_BULLET));
#ifdef JAZZ_PROFILE
    profT[4] += getSubTick() - pt0;
#endif
    if (count) {
        VDP_setSpriteLink(count - 1, 0);
        VDP_updateSprites(count, DMA_QUEUE);
    } else VDP_clearSprites();
}

static u8 prevShield, prevBird;

static void update_sfx(void)
{
    /* Power-up pickups: sound on the frame they switch on. */
    if (game.player.shield && !prevShield) jj1_sfx_start(JJ1_SFX_INVULN, 2, SOUND_PCM_CH4);
    if (game.player.bird && !prevBird) jj1_sfx_start(JJ1_SFX_BIRDY, 2, SOUND_PCM_CH4);
    prevShield = game.player.shield ? 1 : 0;
    prevBird = game.player.bird ? 1 : 0;

    /* Original JJ1 SOUNDS.000 clips, locally converted to signed PCM by the
       importer.  They play through the XGM driver's PCM channels (2-4) so the
       music, which owns channel 1, keeps going underneath.  The XGM sample
       table was primed once in load_video_assets(). */
    /* Priority order: one effect per channel, most important wins. */
    if (game.events & JAZZ_EVENT_HURT)
        jj1_sfx_start(JJ1_SFX_OW, 2, SOUND_PCM_CH2);
    else if (game.events & JAZZ_EVENT_KILL)
        jj1_sfx_start(game.sfxEvent && jj1_event_sfx(game.sfxEvent) ? jj1_event_sfx(game.sfxEvent) : JJ1_SFX_BOOM, 2, SOUND_PCM_CH2);
    else if (game.events & JAZZ_EVENT_FIRE) {
        u8 id = jj1_shot_sfx(game.player.shotType);
        if (id) jj1_sfx_start(id, 1, SOUND_PCM_CH2);
    }
    if (game.events & JAZZ_EVENT_EXIT)
        jj1_sfx_start(JJ1_SFX_YEAHOO, 3, SOUND_PCM_CH3);
    else if (game.events & JAZZ_EVENT_SPRING)
        jj1_sfx_start(JJ1_SFX_SPRING, 1, SOUND_PCM_CH3);
    else if (game.events & JAZZ_EVENT_JUMP)
        jj1_sfx_start(JJ1_SFX_JUMP, 1, SOUND_PCM_CH3);
    if (game.events & JAZZ_EVENT_LIFE)
        jj1_sfx_start(JJ1_SFX_ONEUP, 3, SOUND_PCM_CH4);
    else if (game.sfxEvent && jj1_event_sfx(game.sfxEvent) && !(game.events & (JAZZ_EVENT_HURT | JAZZ_EVENT_KILL)))
        /* the sound the level data gives this event (an item's own pickup
           noise, a breaking block); kills keep the explosion above */
        jj1_sfx_start(jj1_event_sfx(game.sfxEvent), 1, SOUND_PCM_CH4);
    else if (game.events & (JAZZ_EVENT_GEM | JAZZ_EVENT_HEALTH))
        jj1_sfx_start(JJ1_SFX_PICKUP, 1, SOUND_PCM_CH4);
}

static void show_title(void);

static void reset_sprite_slots(void)
{
    u8 sl;
    for (sl = 0; sl < JAZZ_MAX_ENEMIES; sl++) enemySlotArt[sl] = 0xFFFF;
    playerSlotFrame = 0xFFFF;
    boardSlotKey = 0xFFFF;
    bossGunArt = 0xFFFF;
}

/* Plays a bonus level and returns BONUS_WON / BONUS_LOST.  Winning grants a
 * life, as in the original.  The caller rebuilds the screen afterwards. */
static int run_bonus(u8 index)
{
    int r;
    /* the bonus level plays its own music */
    XGM_stopPlay();
    SYS_doVBlankProcess();
    r = bonus_play(index, jj1_music_bonus);
    XGM_stopPlay();
    SYS_doVBlankProcess();
    playingMusic = 0;           /* the stage music must restart */
    if (r == BONUS_WON) {
        if (game.lives < 9) game.lives++;
        jj1_play_bonus_clear(jj1_music_endlevel);
    }
    /* The scene and bonus renderer overwrite the gameplay VRAM allocation. */
    load_video_assets();
    VDP_setWindowOnTop(2);
    VDP_clearPlane(WINDOW, TRUE);
    hud_enter();
    reset_sprite_slots();
    return r;
}

static void begin_play(u8 loaded)
{
    if (!loaded && cheatUnlocked && (selectedStage >= JAZZ_STAGE_COUNT)) {
        /* Stage-select cheat: the BONUS entries start a bonus level directly. */
        jazz_game_init(&game);
        run_bonus((u8)(selectedStage - JAZZ_STAGE_COUNT));
        show_title();
        return;
    }
    /* The original title fills the same VRAM slots as gameplay. */
    load_video_assets();
    if (!loaded) {
        jazz_game_init(&game);
        if (cheatUnlocked && selectedStage) {
            u8 last = (u8)(jj1_pack_stages() - 1);
            jazz_debug_set_stage(&game, (selectedStage > last) ? last : selectedStage);
        }
    }
    game.paused = 0;
    approachedPlanet = 0xFFFF;
    approachedPlanet = jj1_play_planet(game.stage, approachedPlanet);
    playingMusic = 0;
    load_video_assets();
    /* Do not turn a held skip button into a jump or pause on return. */
    playPreviousPad = JOY_readJoypad(JOY_1);
    game.previousInput = input_from_pad(playPreviousPad);
    VDP_setWindowOnTop(2);
    VDP_clearPlane(WINDOW, TRUE);
    hud_enter();
    render_backdrop(game.stage);
    render_level_map();
    renderedStage = game.stage;
    renderedDestroyCount = game.destroyCount;
    for (u8 s = 0; s < JAZZ_MAX_ENEMIES; s++) enemySlotArt[s] = 0xFFFF;
    playerSlotFrame = 0xFFFF;
    boardSlotKey = 0xFFFF;
    bossGunArt = 0xFFFF;
    mode = MODE_PLAY;
}

/* Stage-select cheat.  Enter the pad code on the title screen
 *   UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT B A
 * to unlock it; afterwards UP/DOWN on the title screen choose the stage that
 * START begins on.  Once unlocked it stays on until the console is reset. */
static void draw_level_select(void)
{
    char line[28];
    /*                  0123456789012345678901234     digit at index 18 */
    const char *stageLabel = "STAGE SELECT:  <  1  >    ";
    const char *bonusLabel = "BONUS SELECT:  <  1  >    ";
    const char *label = (selectedStage >= JAZZ_STAGE_COUNT) ? bonusLabel : stageLabel;
    u8 i;
    if (!cheatUnlocked) return;   /* show_title() already blanked the plane */
    for (i = 0; label[i]; i++) line[i] = label[i];
    line[18] = (char)('1' + ((selectedStage >= JAZZ_STAGE_COUNT) ? (selectedStage - JAZZ_STAGE_COUNT) : selectedStage));
    line[i] = 0;
    VDP_setWindowOnBottom(2);
    VDP_drawTextBG(WINDOW, line, 7, 26);
}

/* Feed one newly pressed button set into the cheat-code matcher. */
static void cheat_feed(u16 pressed)
{
    u16 face = (u16)(pressed & (BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT |
                                BUTTON_RIGHT | BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_START));
    u8 key = CHEAT_OTHER;
    if (!face || cheatUnlocked) return;
    if (face == BUTTON_UP) key = CHEAT_UP;
    else if (face == BUTTON_DOWN) key = CHEAT_DOWN;
    else if (face == BUTTON_LEFT) key = CHEAT_LEFT;
    else if (face == BUTTON_RIGHT) key = CHEAT_RIGHT;
    else if (face == BUTTON_B) key = CHEAT_B;
    else if (face == BUTTON_A) key = CHEAT_A;
    if (stage_cheat_step(&cheatProgress, key)) {
        cheatUnlocked = 1;
        draw_level_select();
        jj1_sfx_start(JJ1_SFX_PICKUP, 1, SOUND_PCM_CH4);
    }
}

static void show_title(void)
{
    hud_leave();
    jj1_show_original_title();
    VDP_setTextPlane(WINDOW);
    draw_level_select();
    titlePreviousPad = JOY_readJoypad(JOY_1);
    play_music(jj1_music_menu);
    mode = MODE_TITLE;
}

static void show_main_menu(void)
{
    menuOption = 0;
    jj1_show_original_menu(menuOption);
    VDP_setTextPlane(WINDOW);
    mode = MODE_MENU;
    titlePreviousPad = JOY_readJoypad(JOY_1);
    draw_level_select();
    play_music(jj1_music_menu);
}

static void restore_main_menu(void)
{
    load_video_assets();
    jj1_show_original_menu(menuOption);
    VDP_setTextPlane(WINDOW);
    if (cheatUnlocked) draw_level_select();
    titlePreviousPad = JOY_readJoypad(JOY_1);
    mode = MODE_MENU;
}

/* These are Genesis-specific interfaces, not imitations of the DOS setup. */
static void plain_menu_setup(void)
{
    load_video_assets();
    VDP_setEnable(FALSE);
    VDP_clearSprites(); VDP_updateSprites(1, DMA);
    VDP_clearPlane(BG_A, TRUE); VDP_clearPlane(BG_B, TRUE);
    VDP_clearPlane(WINDOW, TRUE); VDP_setWindowFullScreen();
    VDP_setTextPlane(WINDOW); VDP_setTextPalette(PAL0);
    VDP_setHorizontalScroll(BG_A, 0); VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_A, 0); VDP_setVerticalScroll(BG_B, 0);
    VDP_setEnable(TRUE);
}

static u8 slot_menu(u8 saving)
{
    u8 selected = 0, valid[JJ1_SAVE_SLOTS], i, redraw = 1, message = 0;
    u16 previous = JOY_readJoypad(JOY_1);
    plain_menu_setup();
    for (i = 0; i < JJ1_SAVE_SLOTS; i++) valid[i] = jj1_save_valid(i);
    while (TRUE) {
        u16 pad, pressed;
        if (redraw) {
            VDP_clearPlane(WINDOW, TRUE);
            VDP_drawTextBG(WINDOW, saving ? "SAVE GAME" : "LOAD GAME", 15, 4);
            for (i=0;i<JJ1_SAVE_SLOTS;i++) {
                char line[25]; const char *label=valid[i] ? "   SAVED" : "   EMPTY";
                u8 j=0; line[j++]=i==selected ? 'X' : ' '; line[j++]=' ';
                line[j++]='S'; line[j++]='L'; line[j++]='O'; line[j++]='T';
                line[j++]=' '; line[j++]=(char)('1'+i);
                while (*label) line[j++]=*label++;
                line[j]=0;
                VDP_drawTextBG(WINDOW,line,11,8+i*2);
            }
            if (message) VDP_drawTextBG(WINDOW, saving ? "SAVE FAILED" : "SLOT IS EMPTY", 12, 19);
            VDP_drawTextBG(WINDOW,"UP/DOWN CHOOSE  A/START SELECT",5,23);
            VDP_drawTextBG(WINDOW,"B/C BACK",16,25);
            redraw=0;
        }
        SYS_doVBlankProcess(); pad=JOY_readJoypad(JOY_1); pressed=pad & ~previous; previous=pad;
        if (pressed & (BUTTON_B | BUTTON_C)) return 0;
        if (pressed & BUTTON_UP) { selected=(u8)((selected+3)%4); redraw=1; message=0; }
        if (pressed & BUTTON_DOWN) { selected=(u8)((selected+1)%4); redraw=1; message=0; }
        if (pressed & (BUTTON_A | BUTTON_START)) {
            if (saving ? jj1_save_write(selected,&game) : (valid[selected] && jj1_save_read(selected,&game))) return 1;
            message=1; redraw=1;
        }
    }
}

static void setup_menu(void)
{
    u8 selected=0, redraw=1; u16 previous=JOY_readJoypad(JOY_1);
    static const char *const controls[] = {"A FIRE  B JUMP  C WPN", "B FIRE  A JUMP  C WPN", "C FIRE  B JUMP  A WPN"};
    plain_menu_setup();
    while (TRUE) {
        u16 pad, pressed;
        if (redraw) {
            VDP_clearPlane(WINDOW,TRUE);
            VDP_drawTextBG(WINDOW,"SETUP OPTIONS",13,4);
            VDP_drawTextBG(WINDOW,selected==0?"X MUSIC":"  MUSIC",5,8);
            VDP_drawTextBG(WINDOW,jj1_music_enabled?"ON ":"OFF",25,8);
            VDP_drawTextBG(WINDOW,selected==1?"X SOUND EFFECTS":"  SOUND EFFECTS",5,11);
            VDP_drawTextBG(WINDOW,jj1_sfx_enabled?"ON ":"OFF",25,11);
            VDP_drawTextBG(WINDOW,selected==2?"X CONTROLS":"  CONTROLS",5,14);
            VDP_drawTextBG(WINDOW,controls[jj1_controls],8,16);
            VDP_drawTextBG(WINDOW,"UP/DOWN CHOOSE  LEFT/RIGHT CHANGE",3,22);
            VDP_drawTextBG(WINDOW,"A/START CHANGE    B/C BACK",7,24);
            redraw=0;
        }
        SYS_doVBlankProcess(); pad=JOY_readJoypad(JOY_1); pressed=pad & ~previous; previous=pad;
        if (pressed & (BUTTON_B|BUTTON_C)) break;
        if (pressed & BUTTON_UP) { selected=(u8)((selected+2)%3); redraw=1; }
        if (pressed & BUTTON_DOWN) { selected=(u8)((selected+1)%3); redraw=1; }
        if (pressed & (BUTTON_LEFT|BUTTON_RIGHT|BUTTON_A|BUTTON_START)) {
            if (selected==0) {
                jj1_music_enabled ^= 1; XGM_stopPlay(); playingMusic=0;
                if (jj1_music_enabled) play_music(jj1_music_menu);
            } else if (selected==1) {
                jj1_sfx_enabled ^= 1;
                if (!jj1_sfx_enabled) { XGM_stopPlayPCM(SOUND_PCM_CH2); XGM_stopPlayPCM(SOUND_PCM_CH3); XGM_stopPlayPCM(SOUND_PCM_CH4); }
            } else jj1_controls=(u8)((jj1_controls + ((pressed & BUTTON_LEFT)?2:1))%3);
            jj1_options_save(); redraw=1;
        }
    }
}

static void restore_paused_game(void)
{
    load_video_assets(); VDP_clearPlane(WINDOW,TRUE); VDP_setWindowOnTop(2);
    hud_enter(); render_backdrop(game.stage); render_level_map();
    cameraX = game.player.x > 150 ? game.player.x - 150 : 0;
    cameraY = game.player.y > 96 ? game.player.y - 96 : 0;
    update_camera_and_map(); renderedStage=game.stage; renderedDestroyCount=game.destroyCount;
    reset_sprite_slots(); draw_status_line(&game); render_sprites();
    playPreviousPad=JOY_readJoypad(JOY_1); game.previousInput=input_from_pad(playPreviousPad);
}

static void select_main_menu(void)
{
    if (menuOption == 0) {
        const u8 choice = jj1_play_episode_menu();
        if (choice == 0xFF) { show_main_menu(); return; }
        if (choice == 0xFE) {           /* bonus levels */
            VDP_setEnable(TRUE);        /* the episode screen left it off */
            jazz_game_init(&game);
            run_bonus(0);
            show_title();
            return;
        }
        jj1_pack_select(choice);
        playingMusic = 0;
        begin_play(0);
        return;
    }
    if (menuOption == 5) { show_title(); return; }
    if (menuOption == 1) {
        if (slot_menu(0)) { begin_play(1); return; }
        restore_main_menu(); return;
    }
    if (menuOption == 3) { setup_menu(); restore_main_menu(); return; }
    if (menuOption == 2 || menuOption == 4) {
        jj1_play_info_pages(menuOption == 4);
        restore_main_menu();
        return;
    }
}

static void show_end(const char *title, const char *message)
{
    hud_leave();
    VDP_setWindowFullScreen();
    VDP_clearPlane(WINDOW, TRUE);
    VDP_setTextPlane(WINDOW);
    VDP_drawTextBG(WINDOW, title, 12, 10);
    VDP_drawTextBG(WINDOW, message, 7, 14);
    VDP_drawTextBG(WINDOW, "PRESS START FOR TITLE", 9, 20);
}

static void run_play_frame(u16 input)
{
    u8 completingLevel = (game.transitionTimer == 1 && !game.paused);
    lastInput = input;
#ifdef JAZZ_MAPCHECK
    /* the plane is sent during vblank: check what the last frame left there,
       before the game state moves on */
    if ((renderedStage == game.stage) && (jj1MapOriginX >= 0)) map_check();
#endif
    PROF(0, jazz_step(&game, input));
    if (completingLevel && !game.transitionTimer && !game.paused) {
        if (renderedStage < JAZZ_STAGE_COUNT && JJ1_GAME_PACK->clearScene[renderedStage]) {
            jj1_play_level_clear(jj1_music_endlevel);
            playingMusic = 0;
            load_video_assets();
            VDP_setWindowOnTop(2);
            hud_enter();
            reset_sprite_slots();
        }
    }
    if (game.bonusPending && (renderedStage < JAZZ_STAGE_COUNT)) {
        run_bonus(JJ1_GAME_PACK->bonus[renderedStage]);
        game.bonusPending = 0;
        renderedStage = 0xFF;   /* force the stage to be redrawn below */
    }
    if (completingLevel && !game.transitionTimer && !game.paused) renderedStage = 0xFF;
    if (game.stage != renderedStage) {
        u16 oldPlanet = approachedPlanet;
        if (!game.finished) approachedPlanet = jj1_play_planet(game.stage, approachedPlanet);
        if (approachedPlanet != oldPlanet) {
            playingMusic = 0;
            load_video_assets();
            VDP_setWindowOnTop(2);
            hud_enter();
            reset_sprite_slots();
            /* Consume START/B used to skip the approach, rather than pausing. */
            game.previousInput = input_from_pad(JOY_readJoypad(JOY_1));
        }
        render_backdrop(game.stage);
        render_level_map();
        renderedStage = game.stage;
    }
    update_palette_cycles(0);
    PROF(1, update_camera_and_map());
    blockTm = DMA_QUEUE;   /* the first call filled the screen; scrolling is queued */
    if (game.destroyCount != renderedDestroyCount) {
        draw_jj1_block(game.destroyedX, game.destroyedY);
        send_jj1_block(game.destroyedX, game.destroyedY);
        renderedDestroyCount = game.destroyCount;
    }
    PROF(2, draw_status_line(&game));
    PROF(3, render_sprites());
    PROF(4, update_sfx());
    if (game.finished) {
        if (game.lives) {
            jj1_play_episode_ending(jj1_music_bonus);
            playingMusic = 0;
            load_video_assets();
            show_title();
        } else {
            mode = MODE_GAMEOVER;
            show_end("GAME OVER", "TRY AGAIN, JAZZ!");
        }
    }
}

#ifdef JAZZ_AUTOTEST
static u8 run_target_point_to_point_tests(void)
{
    JazzGame *probe = &game;
    s16 startX;

    /* This executes in the actual 68000 binary. Keep it bounded and use the
       global game state: the Genesis user stack is intentionally only 512 B. */
    jazz_game_init(probe);
    if ((probe->lives != 3) || (probe->health != 5)) return 0;
    /* The spawn settle must have found real mask ground under Jazz. */
    if (!jj1_runtime_down_point_solid(probe->stage, probe->player.x + 7,
                                      probe->player.y + 22)) return 0;
    /* The original level must contain items and an end-of-level event. */
    if (!probe->stageGemTotal) return 0;
    startX = probe->player.x;
    { u8 i; for (i = 0; i < 4; i++) jazz_step(probe, JAZZ_INPUT_RIGHT); }
    if (probe->player.x <= startX) return 0;

    /* Palette cycling: after a second of frames every active range must have
       advanced, and CRAM must hold the colour its phase calls for. */
    stageArt = &jazz_stage_art[2];
    reset_palette_cycles();
    { u8 i; for (i = 0; i < 70; i++) update_palette_cycles(1); }
    { u8 i, moved = 0;
      for (i = 0; i < stageArt->cycleCount; i++) {
          const Jj1Cycle *c = &stageArt->cycles[i];
          if (!(stageArt->cycleMask & c->bit)) continue;
          moved++;
          { u8 steps = 70 / c->stepFrames, k = c->count;
            u8 want = (c->dir > 0) ? (u8)((k - (steps % k)) % k) : (u8)(steps % k);
            if (cyclePhase[i] != want) return 2; }
          if (PAL_getColor((PAL2 * 16) + c->slot) != c->colors[cyclePhase[i] % c->count]) return 3;
      }
      if (!moved) return 4; }

    /* Guardian activation is covered by the host tests, which can afford to
       search the level data for the arena.  Doing that scan here costs tens of
       thousands of event lookups on the 68000 at boot, which stalls the probe
       long enough to look like a hang. */
    return 1;
}
#endif

int main(bool hardReset)
{
    u16 pad;
    (void)hardReset;
    /* Bring episode 1's game pack into the window before anything reads it. */
    /* The lite program keeps the level map and event grid in RAM (32 KB), so
       the DMA queue gets a 4 KB buffer instead of SGDK's 8 KB.  SGDK's own
       start-up allocation did not fit; this one is made again here. */
    DMA_initEx(0, 0xFFFF, 4096);
    jj1_pack_select(0);
#ifdef JAZZ_MEMPROBE
    { u16 f = MEM_getFree(), l = MEM_getLargestFreeBlock(); SRAM_enable(); SRAM_writeWord(0, 0x4D45); SRAM_writeWord(2, f); SRAM_writeWord(4, l); SRAM_disable(); }
#endif
#ifdef JAZZ_AUTOTEST
    /* Boot marker: distinct from the final assertion marker below. */
    SRAM_enable();
    SRAM_writeByte(0, 'B'); SRAM_writeByte(1, 'O');
    SRAM_writeByte(2, 'O'); SRAM_writeByte(3, 'T');
    SRAM_writeByte(4, 0); SRAM_disable();
#endif
    load_video_assets();
#ifdef JAZZ_SFX_TEST
    /* Isolated emulator probe; JAZZ_SFX_TEST is the JAZZ_SHOT_* index.
     * The release always enters the normal game path instead. */
    XGM_stopPlay();
    for (u16 f = 0; f < 60; f++) SYS_doVBlankProcess();
    game.player.shotType = JAZZ_SFX_TEST;
    game.events = JAZZ_EVENT_FIRE;
    update_sfx();
    while (TRUE) SYS_doVBlankProcess();
#endif
#ifdef JAZZ_AUTOTEST
    u8 probeResult;
    probeResult = run_target_point_to_point_tests();
    if (probeResult == 1) {
        /* A deterministic SRAM marker lets the external emulator runner
         * assert the result without scraping a window or debugger console. */
        SRAM_enable();
        SRAM_writeByte(0, 'J'); SRAM_writeByte(1, 'A');
        SRAM_writeByte(2, 'Z'); SRAM_writeByte(3, 'P');
        SRAM_writeByte(4, 1); SRAM_disable();
        KLog("JAZZ_P2P_PASS");
    } else {
        SRAM_enable();
        SRAM_writeByte(0, 'J'); SRAM_writeByte(1, 'A');
        SRAM_writeByte(2, 'Z'); SRAM_writeByte(3, 'P');
        SRAM_writeByte(4, probeResult); SRAM_disable();
        KLog("JAZZ_P2P_FAIL");
    }
    while (TRUE) SYS_doVBlankProcess();
#else
    JOY_init();
    jj1_options_load();
#ifdef JAZZ_SCENE_TEST
    /* Dedicated integration-test ROM; never enabled in release builds. */
    jj1_play_level_clear(jj1_music_endlevel);
    jj1_play_bonus_clear(jj1_music_endlevel);
    load_video_assets();
    playingMusic = 0;
#endif
#ifdef JAZZ_PLANET_TEST
    approachedPlanet = jj1_play_planet(JAZZ_PLANET_TEST, 0xFFFF);
    approachedPlanet = jj1_play_planet(JAZZ_PLANET_TEST, approachedPlanet);
    playingMusic = 0;
    load_video_assets();
    show_title();
#elif defined(JAZZ_EPISODE_FLOW_TEST)
    begin_play(0);
    jazz_debug_set_stage(&game, JAZZ_STAGE_COUNT - 1);
    renderedStage = JAZZ_STAGE_COUNT - 1;
    game.transitionTimer = 1;
#elif defined(JAZZ_BONUS_END_TEST)
    jazz_game_init(&game);
    run_bonus(0);
    playingMusic = 0;
    load_video_assets();
    show_title();
#elif defined(JAZZ_ENDING_TEST)
#ifdef JAZZ_TEST_EPISODE
    jj1_pack_select(JAZZ_TEST_EPISODE);
#endif
    jj1_play_episode_ending(jj1_music_bonus);
    playingMusic = 0;
    load_video_assets();
    show_title();
#elif defined(JAZZ_SCENE_FLOW_TEST)
    /* Exercise the real gameplay -> transition -> gameplay path in the
     * emulator without requiring an entire manual level playthrough. */
    begin_play(0);
    jazz_debug_set_stage(&game, 1);  /* LEVEL1.000 requests ENDLEVEL.0SC */
    renderedStage = 1;
    game.transitionTimer = 1;
#else
#ifndef JAZZ_SCENE_TEST
    jj1_play_startup();
#endif
    show_title();
#endif
    while (TRUE) {
        pad = JOY_readJoypad(JOY_1);
        if (mode == MODE_TITLE) {
            u16 pressed = (u16)(pad & ~titlePreviousPad);
            if (cheatUnlocked) {
                if ((pressed & BUTTON_UP) && (selectedStage + 1 < JAZZ_STAGE_COUNT + BONUS_LEVELS)) {
                    selectedStage++;
                    draw_level_select();
                } else if ((pressed & BUTTON_DOWN) && selectedStage) {
                    selectedStage--;
                    draw_level_select();
                }
            } else {
                cheat_feed(pressed);
            }
            titlePreviousPad = pad;
            if (pressed & BUTTON_START) show_main_menu();
        } else if (mode == MODE_MENU) {
            u16 pressed = (u16)(pad & ~titlePreviousPad);
            if (!cheatUnlocked) cheat_feed(pressed);
            if (pressed & (BUTTON_UP | BUTTON_DOWN)) {
                if (pressed & BUTTON_UP) menuOption = (u8)((menuOption + 5) % 6);
                else menuOption = (u8)((menuOption + 1) % 6);
                jj1_set_menu_option(menuOption);
                VDP_clearPlane(WINDOW, TRUE);
                if (cheatUnlocked) draw_level_select();
                else VDP_setWindowOnTop(0);
            }
            if (cheatUnlocked) {
                if ((pressed & BUTTON_RIGHT) && selectedStage + 1 < JAZZ_STAGE_COUNT + BONUS_LEVELS) {
                    selectedStage++; draw_level_select();
                } else if ((pressed & BUTTON_LEFT) && selectedStage) {
                    selectedStage--; draw_level_select();
                }
            }
            titlePreviousPad = pad;
            if (pressed & BUTTON_START) select_main_menu();
            else if (pressed & BUTTON_C) show_title();
        } else if (mode == MODE_PLAY) {
            if (game.paused && (pad & BUTTON_C) && !(playPreviousPad & BUTTON_C)) {
                slot_menu(1); restore_paused_game();
            } else {
                playPreviousPad=pad;
#ifdef JAZZ_AUTOPILOT
                {   /* test builds: input from the play iteration, not the pad */
                    static u16 it;
                    u16 in = (it % 1200 < 800) ? JAZZ_INPUT_RIGHT : JAZZ_INPUT_LEFT;
                    if ((it % 70) < 18) in |= JAZZ_INPUT_JUMP;
                    if ((it % 400) > 380) in |= JAZZ_INPUT_DOWN;
                    it++;
#ifdef JAZZ_PROFDUMP
                    {
                        static u16 lastV2;
                        u32 b0 = getSubTick(), b1;
                        u16 dv = (u16)(vtimer - lastV2); lastV2 = vtimer;
                        run_play_frame(in);
                        b1 = getSubTick() - b0;
                        if (it > 60) {
                            u16 q = DMA_getQueueTransferSize();
                            if (q > profDmaMax) profDmaMax = q;
                            if (q > 7200) profDmaOver++;
                        }
                        if (it > 60) {      /* skip level set-up */
                            profBusy += b1; profIter++;
                            if (b1 > profBusyMax && b1 < 60000) profBusyMax = (u16)b1;
                            profHist[dv > 5 ? 5 : dv]++;
                            if (b1 > 1150) {    /* over ~15 ms: a frame at risk */
                                u8 q; profOverN++;
                                for (q = 0; q < 8; q++) profOver[q] += profCur[q];
                                if (profOverN <= 150) {
                                    u16 o = (u16)(256 + (profOverN - 1) * 10);
                                    SRAM_enable();
                                    SRAM_writeByte(o++, (u8)(it >> 8)); SRAM_writeByte(o++, (u8)it);
                                    for (q = 0; q < 8; q++) { u32 v = profCur[q] / 10; SRAM_writeByte(o++, (u8)(v > 255 ? 255 : v)); }
                                    SRAM_disable();
                                }
                            }
                        }
                        { u8 q; for (q = 0; q < 8; q++) profCur[q] = 0; }
                        if (it == 60) { u8 q; for (q = 0; q < 8; q++) profSum[q] = 0; }
                        if (it == JAZZ_PROFDUMP) {
                            u16 o = 0; u8 q, k;
                            SRAM_enable();
                            SRAM_writeByte(o++, 'P'); SRAM_writeByte(o++, 'R');
                            for (q = 0; q < 8; q++) for (k = 0; k < 4; k++) SRAM_writeByte(o++, (u8)(profSum[q] >> (24 - 8 * k)));
                            for (k = 0; k < 4; k++) SRAM_writeByte(o++, (u8)(profBusy >> (24 - 8 * k)));
                            for (k = 0; k < 4; k++) SRAM_writeByte(o++, (u8)(profIter >> (24 - 8 * k)));
                            SRAM_writeByte(o++, (u8)(profBusyMax >> 8)); SRAM_writeByte(o++, (u8)profBusyMax);
                            for (q = 0; q < 6; q++) { SRAM_writeByte(o++, (u8)(profHist[q] >> 8)); SRAM_writeByte(o++, (u8)profHist[q]); }
                            for (q = 0; q < 8; q++) for (k = 0; k < 4; k++) SRAM_writeByte(o++, (u8)(profOver[q] >> (24 - 8 * k)));
                            for (k = 0; k < 4; k++) SRAM_writeByte(o++, (u8)(profOverN >> (24 - 8 * k)));
                            SRAM_writeByte(o++, (u8)(profDmaMax >> 8)); SRAM_writeByte(o++, (u8)profDmaMax);
                            SRAM_writeByte(o++, (u8)(profDmaOver >> 8)); SRAM_writeByte(o++, (u8)profDmaOver);
                            SRAM_disable();
                        }
                    }
#else
                    run_play_frame(in);
#endif
#ifdef JAZZ_MAPCHECK
                    if (it == 800) {
                        /* result for the test script: M,A,P,C then 1 = no mismatch (windows needing more than 64 distinct blocks cannot all be cached and are not counted) */
                        SRAM_enable();
                        SRAM_writeByte(0, 'M'); SRAM_writeByte(1, 'A');
                        SRAM_writeByte(2, 'P'); SRAM_writeByte(3, 'C');
                        SRAM_writeByte(4, mapCheckBadSmall ? 2 : 1);
                        SRAM_disable();
                    }
#endif
                    {
                        char t[8];
                        intToStr(it, t, 5); VDP_drawTextBG(WINDOW, t, 17, 1);
                    }
                }
#else
                run_play_frame(input_from_pad(pad));
#endif
            }
#ifdef JAZZ_PROFILE
            { static u8 t; static u16 lastV, missed, worst, total;
              u16 dv = (u16)(vtimer - lastV); lastV = vtimer;
              if (dv > 1 && dv < 60) { missed += dv - 1; total += dv - 1; if (dv > worst) worst = dv; }
              if (!(++t & 31)) {
                char b[40]; u8 i, n = 0;
                /* average microseconds per frame of each stage: step, camera,
                   status, sprites, sfx */
                for (i = 0; i < 8; i++) {
                    u16 us = (u16)((profT[i] / 32) * 13 / 1000 * 1000 / 1000);
                    /* subtick = 13.02 us; show tenths of a millisecond */
                    us = (u16)((profT[i] / 32) * 13 / 100);
                    b[n++] = (char)('0' + us / 10 % 10);
                    b[n++] = (char)('0' + us % 10); b[n++] = ' ';
                    profT[i] = 0;
                }
                b[n++] = (char)('0' + (missed > 9 ? 9 : missed));
                b[n++] = (char)('0' + (worst > 9 ? 9 : worst));
                missed = 0; worst = 0;
                VDP_drawTextBG(WINDOW, "    ", 36, 0);
                { char tt[6]; intToStr(total, tt, 4); VDP_drawTextBG(WINDOW, tt, 36, 0); }
                b[n] = 0;
                VDP_drawTextBG(WINDOW, b, 12, 1); } }
#endif
        } else if (pad & BUTTON_START) {
            show_title();
        }
#ifdef JAZZ_FIXED_PACE
        /* test builds: every loop iteration takes exactly two frames, so two
           builds of different speed can be compared screenshot for screenshot */
        { static u16 paceStart; SYS_doVBlankProcess();
          while ((u16)(vtimer - paceStart) < JAZZ_FIXED_PACE) SYS_doVBlankProcess();
          paceStart = vtimer; }
#else
        SYS_doVBlankProcess();
#endif
    }
#endif
    return 0;
}
