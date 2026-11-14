/* Platform-neutral JJ1 runtime: level selection, 4x4px mask collision,
 * event-grid queries, springs, spawn placement, end-of-level detection.
 * No VDP/controller/sound calls, so the exact same code drives the Genesis
 * ROM and the host point-to-point tests against the real converted maps. */

#include "jazz_game.h"
#include "jj1_events.h"
#include "jj1_runtime.h"

typedef struct {
    const u8 *blocks;
    const u8 *masks;
    const u8 *events;
    u16 startX, startY;
} Jj1LevelData;

/* One entry per stage.  The shareware ships eight levels across three worlds;
 * the map, event grid and masks are per level, while the 120KB tile bank is
 * shared by every level in a world (see gfx.c). */
#define JJ1_LEVEL(n) { jj1_level##n##_blocks, jj1_level##n##_masks, \
                       jj1_level##n##_events, 0, 0 }

#ifdef JAZZ_HOST
static Jj1LevelData jj1_level_data(u8 stage)
{
    Jj1LevelData d;
    /* One case per stage, generated for the episode the tests are built on. */
    switch (stage) {
#include "jj1_episode_leveldata.inc"
    default:
        d.blocks = jj1_level0_blocks; d.masks = jj1_level0_masks;
        d.events = jj1_level0_events;
        d.startX = jj1_level0_start_x; d.startY = jj1_level0_start_y; break;
    }
    return d;
}
u8 jj1_runtime_stage_count(void) { return JAZZ_STAGE_COUNT; }
#else
/* On the cartridge the levels come from the episode pack mapped at $200000. */
#include "jj1_pack.h"
/* Lite cartridge: the map and event grid are aPLib-packed and unpacked into
 * RAM when the stage starts.  The rest of the program reads them through the
 * same pointers as on the full cartridge. */
static u8 liteMap[256 * 64];
static u8 liteEvents[256 * 64];
static Jj1LevelData jj1_level_data(u8 stage)
{
    const Jj1PackLevel *src = &JJ1_GAME_PACK->level[(stage < JJ1_PACK_STAGES) ? stage : 0];
    Jj1LevelData d;
    aplib_unpack((u8 *)src->blocks, liteMap);
    aplib_unpack((u8 *)src->events, liteEvents);
    d.blocks = liteMap; d.masks = src->masks; d.events = liteEvents;
    d.startX = src->startX; d.startY = src->startY;
    return d;
}
u8 jj1_runtime_stage_count(void) { return JJ1_GAME_PACK->stages; }
#endif

/* Forget the cached stage: another episode's pack is about to be mapped, and
 * its stage numbers are the same. */
void jj1_runtime_invalidate(void);

/* The queries below run dozens of times per frame (every collision probe and
 * every cell of the visible event window), so the per-stage data is resolved
 * once and kept: the pointer table, a flat class byte per event id, and
 * whether the level has any bridge at all (most do not, and finding out costs
 * a nine-cell scan per probe). */
#define JJ1_MAX_BRIDGES 48
static struct {
    u8 stage;
    Jj1LevelData d;
    u8 klass[128];
    u8 hasBridge;
    u8 bridgeRows[8];       /* bit per grid row that holds a bridge event */
    /* The bridges themselves, in row order, so a probe on a bridge row
       checks that row's few spans instead of scanning nine grid cells. */
    struct { s16 startX, endX, deckY; u8 blockX; } bridge[JJ1_MAX_BRIDGES];
    u8 bridgeFirst[65];     /* index of each row's first bridge */
    u8 bridgeListed;        /* the list holds every bridge of the level */
} L = { .stage = 0xFF };

static Jj1Fast F = { 0, 0, 0, 0, 0, 0xFF, 0 };

void jj1_runtime_invalidate(void) { L.stage = 0xFF; F.stage = 0xFF; }
#ifndef JAZZ_HOST
/* The 32 KB is free outside a level: the menus may borrow it. */
u8 *jj1_runtime_scratch(void) { L.stage = 0xFF; F.stage = 0xFF; return liteMap; }
#endif

static const Jj1LevelData *lvl(u8 stage)
{
    if (L.stage != stage) {
        u16 i;
        L.d = jj1_level_data(stage);
        for (i = 0; i < 128; i++) L.klass[i] = jj1_event_info(stage, (u8)i)->klass;
        L.hasBridge = 0;
        for (i = 0; i < 8; i++) L.bridgeRows[i] = 0;
        {
            u8 count = 0;
            u16 row, x;
            const u8 *ev = L.d.events;
            L.bridgeListed = 1;
            for (row = 0; row < 64; row++) {
                L.bridgeFirst[row] = count;
                for (x = 0; x < 256; x++) {
                    const u8 e = *ev++;
                    const Jj1EventInfo *info;
                    if ((e >= 128) || (L.klass[e] != JJ1_CLASS_BRIDGE)) continue;
                    L.hasBridge = 1;
                    L.bridgeRows[row >> 3] |= (u8)(1 << (row & 7));
                    if (count >= JJ1_MAX_BRIDGES) { L.bridgeListed = 0; continue; }
                    info = jj1_event_info(stage, e);
                    L.bridge[count].blockX = (u8)x;
                    L.bridge[count].startX = (s16)(x << 5);
                    L.bridge[count].endX = (s16)((x << 5) + (s16)info->param * (s16)info->strength);
                    L.bridge[count].deckY = (s16)((row << 5) + info->points);
                    count++;
                }
            }
            L.bridgeFirst[64] = count;
        }
        L.stage = stage;
        F.stage = 0xFF;
    }
    return &L.d;
}

/* Class of the event in a grid cell from the cached table: the same as
 * jj1_event_info(stage, jj1_runtime_event(stage, x, y))->klass (an out-of-range
 * cell and ids >= 128 read as event 0 / JJ1_CLASS_NONE there too). */
static u8 cell_klass(u8 stage, s16 blockX, s16 blockY)
{
    const Jj1LevelData *d = lvl(stage);      /* also fills L.klass */
    u8 e;
    if ((blockX < 0) || (blockX >= 256) || (blockY < 0) || (blockY >= 64)) return L.klass[0];
    e = d->events[((u16)blockY << 8) + blockX];
    return (e < 128) ? L.klass[e] : JJ1_CLASS_NONE;
}


const u8 *jj1_runtime_events(u8 stage)
{
    return lvl(stage)->events;
}

const u8 *jj1_runtime_blocks(u8 stage)
{
    return lvl(stage)->blocks;
}

const Jj1Fast *jj1_runtime_fast(u8 stage)
{
    const Jj1LevelData *d = lvl(stage);
    if (F.stage != stage) {
        F.events = d->events; F.blocks = d->blocks; F.masks = d->masks;
        F.klass = L.klass; F.bridgeRows = L.bridgeRows;
        F.hasBridge = L.hasBridge; F.stage = stage;
    }
    return &F;
}

const u8 *jj1_runtime_klass_table(u8 stage)
{
    (void)lvl(stage);
    return L.klass;
}

u8 jj1_runtime_klass(u8 stage, u8 id)
{
    (void)lvl(stage);
    return (id < 128) ? L.klass[id] : 0;
}

u8 jj1_runtime_event(u8 stage, s16 blockX, s16 blockY)
{
    if ((blockX < 0) || (blockX >= 256) || (blockY < 0) || (blockY >= 64)) return 0;
    return lvl(stage)->events[((u16)blockY << 8) + blockX];
}

/* JJ1 masks are 8x8 cells per 32x32 block: one bit is a 4x4-pixel collision
 * cell, accurate enough for slopes/ramp stepping on a 7.67 MHz 68000. */
/* Is this 4x4 mask cell on a bridge deck?  One bridge event spans `param`
 * pieces spaced `strength` pixels apart, with the deck `points` pixels below
 * the cell's top edge, so a span reaches several blocks to the right of the
 * event that owns it. */
static u8 bridge_deck(u8 stage, s16 cellX, s16 cellY)
{
    const Jj1LevelData *d = lvl(stage);
    s16 blockY = (s16)((u16)cellY >> 3);
    s16 blockX = (s16)(cellX >> 3);
    s16 px = (s16)(cellX << 2);            /* left edge of this mask cell */
    s16 py = (s16)(cellY << 2);
    s16 scan;
    if (!L.hasBridge || (blockY < 0) || (blockY > 63)) return 0;
    if (!(L.bridgeRows[blockY >> 3] & (1 << (blockY & 7)))) return 0;
    if (L.bridgeListed) {
        /* the same test as the scan below, over this row's listed spans */
        u8 n;
        for (n = L.bridgeFirst[blockY]; n < L.bridgeFirst[blockY + 1]; n++) {
            s16 bx = L.bridge[n].blockX;
            if ((bx > blockX) || (bx <= blockX - 9)) continue;
            if ((px >= L.bridge[n].startX) && (px < L.bridge[n].endX) &&
                (py + 4 > L.bridge[n].deckY) && (py < L.bridge[n].deckY + 8)) return 1;
        }
        return 0;
    }
    /* A span is at most 255 px, so eight blocks back always covers it. */
    for (scan = blockX; (scan >= 0) && (scan > blockX - 9); scan--) {
        u8 e = d->events[(((u16)blockY) << 8) + scan];
        const Jj1EventInfo *info;
        s16 startX, deckY, span;
        if ((e >= 128) || (L.klass[e] != JJ1_CLASS_BRIDGE)) continue;
        info = jj1_event_info(stage, e);
        startX = (s16)(scan << 5);
        span = (s16)((s16)info->param * (s16)info->strength);
        deckY = (s16)((blockY << 5) + info->points);
        if ((px >= startX) && (px < startX + span) &&
            (py + 4 > deckY) && (py < deckY + 8)) return 1;
    }
    return 0;
}

static u8 jj1_runtime_mask_cell(u8 stage, s16 cellX, s16 cellY, u8 allowOneWay)
{
    const Jj1LevelData *d;
    u8 block;
    if ((cellX < 0) || (cellX >= (256 * 8)) || (cellY < 0) || (cellY >= (64 * 8))) return 1;
    d = lvl(stage);
    {
        u8 event = d->events[(((u16)cellY >> 3) << 8) + (cellX >> 3)];
        u8 klass = (event < 128) ? L.klass[event] : 0;
        /* One-way platforms (JJ1 modifier 6, grid event 122): only downward
           feet probes may land. */
        if (!allowOneWay && (klass == JJ1_CLASS_ONEWAY)) return 0;
        /* A bridge (movement 28) is drawn by the event, not the tile, so the
           block behind it is usually empty.  One event spans multiA pieces of
           pieceSize*4 px, so the deck extends well past its own cell: scan back
           along the row for a bridge whose span reaches this column.  Like a
           one-way platform it is only solid from above. */
        if (klass == JJ1_CLASS_BRIDGE) return allowOneWay ? bridge_deck(stage, cellX, cellY) : 0;
    }
    if (allowOneWay && bridge_deck(stage, cellX, cellY)) return 1;
    block = d->blocks[(((u16)cellY >> 3) << 8) + (cellX >> 3)];
    if (block >= 240) return 0;
    return (d->masks[((u16)block << 3) + (cellY & 7)] & (1 << (cellX & 7))) ? 1 : 0;
}

u8 jj1_runtime_point_solid(u8 stage, s16 x, s16 y)
{
    return jj1_runtime_mask_cell(stage, x >> 2, y >> 2, 0);
}

u8 jj1_runtime_down_point_solid(u8 stage, s16 x, s16 y)
{
    return jj1_runtime_mask_cell(stage, x >> 2, y >> 2, 1);
}

/* Solid for the game: the mask says so and the 32 px block has not been shot
 * away (destroyed: the game's bit per grid cell).  Off-grid blocks cannot be
 * destroyed. */
static u8 block_destroyed(const u8 *destroyed, s16 gx, s16 gy)
{
    u16 bit;
    if ((gx < 0) || (gx > 255) || (gy < 0) || (gy > 63)) return 0;
    bit = ((u16)gy << 8) + (u16)gx;
    return (u8)((destroyed[bit >> 3] >> (bit & 7)) & 1);
}

u8 jj1_runtime_bridge_deck(u8 stage, s16 cellX, s16 cellY)
{
    return bridge_deck(stage, cellX, cellY);
}

u8 jj1_runtime_point_solid_d(u8 stage, s16 x, s16 y, u8 allowOneWay, const u8 *destroyed)
{
    if (!jj1_runtime_mask_cell(stage, x >> 2, y >> 2, allowOneWay)) return 0;
    return !block_destroyed(destroyed, x >> 5, y >> 5);
}

/* Every 4 px mask cell the rectangle touches (the caller has checked that it
 * lies inside the world). */
u8 jj1_runtime_rect_solid_d(u8 stage, s16 x, s16 y, s16 w, s16 h, const u8 *destroyed)
{
    s16 cx, cy;
    s16 cx0 = x >> 2, cx1 = (s16)((x + w - 1) >> 2);
    s16 cy0 = y >> 2, cy1 = (s16)((y + h - 1) >> 2);
    for (cy = cy0; cy <= cy1; cy++)
        for (cx = cx0; cx <= cx1; cx++)
            if (jj1_runtime_mask_cell(stage, cx, cy, 0) &&
                !block_destroyed(destroyed, cx >> 3, cy >> 3)) return 1;
    return 0;
}

u8 jj1_runtime_rect_solid(u8 stage, s16 x, s16 y, s16 w, s16 h)
{
    s16 cx, cy;
    s16 cx0 = x >> 2;
    s16 cx1 = (x + w - 1) >> 2;
    s16 cy0 = y >> 2;
    s16 cy1 = (y + h - 1) >> 2;
    for (cy = cy0; cy <= cy1; cy++)
        for (cx = cx0; cx <= cx1; cx++)
            if (jj1_runtime_mask_cell(stage, cx, cy, 0)) return 1;
    return 0;
}

u8 jj1_runtime_solid(u8 stage, s16 tx, s16 ty)
{
    return jj1_runtime_rect_solid(stage, tx << 4, ty << 4, 16, 16);
}

/* Upward springs share JJ1 modifier 29.  As in the original engine a spring
 * does not set a velocity directly: it retargets the jump ascent so the
 * player's FEET rise to |magnitude| * 21 pixels above the spring block, and
 * the shared ascent rule (see game_core.c) produces the launch arc.  The
 * event's param carries the absolute magnitude.  Returns the target feet
 * height in pixels above the spring block, or 0. */
u16 jj1_runtime_spring_height(u8 stage, s16 x, s16 y, s16 w, s16 h, s16 *springTopY)
{
    s16 bx0 = x >> 5;
    s16 bx1 = (x + w - 1) >> 5;
    s16 by0 = y >> 5;
    s16 by1 = (y + h - 1) >> 5;
    s16 bx, by;
    for (by = by0; by <= by1; by++) {
        for (bx = bx0; bx <= bx1; bx++) {
            const Jj1EventInfo *info;
            if (cell_klass(stage, bx, by) != JJ1_CLASS_SPRING) continue;
            info = jj1_event_info(stage, jj1_runtime_event(stage, bx, by));
            if (info->klass == JJ1_CLASS_SPRING) {
                if (springTopY) *springTopY = by << 5;
                return (u16)info->param * 21;
            }
        }
    }
    return 0;
}

/* Sucker tube (JJ1 movement 37/38).  The tube's own event data says whether it
 * is horizontal or a vertical launch, so nothing is inferred from geometry:
 *   - a horizontal tube pushes at magnitude * 171 (8.8 px/frame), sign from the
 *     magnitude byte (info->param);
 *   - a vertical launch (info->strength != 0) fires the player upward toward a
 *     target height of strength*3 tiles above the tube cell, exactly as the
 *     original's REPELUP does, with a magnitude-signed horizontal drift.
 * Returns 0 when the box is in no tube; 1 for a horizontal tube (fills *pushX);
 * 2 for a vertical launch (fills *pushX drift and *targetY, the pixel Y the
 * player should be lifted to). */
u8 jj1_runtime_tube_push(u8 stage, s16 x, s16 y, s16 w, s16 h,
                         s16 *pushX, s16 *targetY)
{
    s16 bx0 = x >> 5;
    s16 bx1 = (x + w - 1) >> 5;
    s16 by0 = y >> 5;
    s16 by1 = (y + h - 1) >> 5;
    s16 bx, by;
    *pushX = 0;
    *targetY = 0;
    for (by = by0; by <= by1; by++) {
        for (bx = bx0; bx <= bx1; bx++) {
            const Jj1EventInfo *info;
            if (cell_klass(stage, bx, by) != JJ1_CLASS_TUBE) continue;
            info = jj1_event_info(stage, jj1_runtime_event(stage, bx, by));
            if (info->klass == JJ1_CLASS_TUBE) {
                s16 mag = (s16)(s8)info->param;          /* signed horizontal */
                *pushX = (s16)(mag * 171);
                if (info->strength) {
                    /* Vertical launch: target height above this cell.  The
                       original is targetY = TTOF(gridY) - ITOF(multiA * 3);
                       since ITOF and the pixel scale share the same fixed shift,
                       that is multiA * 3 pixels above the cell's top edge. */
                    *targetY = (s16)((by << 5) - ((s16)info->strength * 3));
                    return 2;
                }
                return 1;
            }
        }
    }
    return 0;
}

/* End-of-level sign (JJ1 modifier 27). */
u8 jj1_runtime_touches_end(u8 stage, s16 x, s16 y, s16 w, s16 h)
{
    s16 bx0 = x >> 5;
    s16 bx1 = (x + w - 1) >> 5;
    s16 by0 = y >> 5;
    s16 by1 = (y + h - 1) >> 5;
    s16 bx, by;
    for (by = by0; by <= by1; by++)
        for (bx = bx0; bx <= bx1; bx++)
            if (cell_klass(stage, bx, by) == JJ1_CLASS_END) return 1;
    return 0;
}

void jj1_runtime_place_player(JazzGame *state, u8 stage)
{
    const Jj1LevelData *d = lvl(stage);
    s16 spawnY;
    s16 spawnX = ((s16)d->startX << 5) + 8;
    spawnY = ((s16)d->startY << 5) - 22;
    if (spawnY < 0) spawnY = 0;
    /* The JJ1 start record names the spawn grid point; settle it against the
       real mask so Jazz begins on the actual slope/platform surface. */
    while ((spawnY < ((64 * 32) - 23)) &&
           !jj1_runtime_down_point_solid(stage, spawnX + 2, spawnY + 22) &&
           !jj1_runtime_down_point_solid(stage, spawnX + 7, spawnY + 22) &&
           !jj1_runtime_down_point_solid(stage, spawnX + 11, spawnY + 22)) spawnY++;
    state->player.x = spawnX;
    state->player.y = spawnY;
    state->player.subX = 0;
    state->player.subY = 0;
    state->player.vx = 0;
    state->player.vy = 0;
    state->player.jumpTargetY = JAZZ_NO_JUMP_TARGET;
    state->player.onGround = 1;
}
