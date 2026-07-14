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

static Jj1LevelData jj1_level_data(u8 stage)
{
    Jj1LevelData d;
    if (stage == 1) {
        d.blocks = jj1_level1_blocks; d.masks = jj1_level1_masks;
        d.events = jj1_level1_events;
        d.startX = jj1_level1_start_x; d.startY = jj1_level1_start_y;
    } else if (stage >= 2) {
        d.blocks = jj1_level2_blocks; d.masks = jj1_level2_masks;
        d.events = jj1_level2_events;
        d.startX = jj1_level2_start_x; d.startY = jj1_level2_start_y;
    } else {
        d.blocks = jj1_level0_blocks; d.masks = jj1_level0_masks;
        d.events = jj1_level0_events;
        d.startX = jj1_level0_start_x; d.startY = jj1_level0_start_y;
    }
    return d;
}

u8 jj1_runtime_event(u8 stage, s16 blockX, s16 blockY)
{
    Jj1LevelData d;
    if ((blockX < 0) || (blockX >= 256) || (blockY < 0) || (blockY >= 64)) return 0;
    d = jj1_level_data(stage);
    return d.events[((u16)blockY << 8) + blockX];
}

/* JJ1 masks are 8x8 cells per 32x32 block: one bit is a 4x4-pixel collision
 * cell, accurate enough for slopes/ramp stepping on a 7.67 MHz 68000. */
static u8 jj1_runtime_mask_cell(u8 stage, s16 cellX, s16 cellY, u8 allowOneWay)
{
    Jj1LevelData d;
    u8 block;
    if ((cellX < 0) || (cellX >= (256 * 8)) || (cellY < 0) || (cellY >= (64 * 8))) return 1;
    d = jj1_level_data(stage);
    /* One-way platforms (JJ1 modifier 6, grid event 122): only downward
       feet probes may land. */
    if (!allowOneWay &&
        (jj1_event_info(stage, d.events[(((u16)cellY >> 3) << 8) + (cellX >> 3)])->klass
         == JJ1_CLASS_ONEWAY)) return 0;
    block = d.blocks[(((u16)cellY >> 3) << 8) + (cellX >> 3)];
    if (block >= 240) return 0;
    return (d.masks[((u16)block << 3) + (cellY & 7)] & (1 << (cellX & 7))) ? 1 : 0;
}

u8 jj1_runtime_point_solid(u8 stage, s16 x, s16 y)
{
    return jj1_runtime_mask_cell(stage, x >> 2, y >> 2, 0);
}

u8 jj1_runtime_down_point_solid(u8 stage, s16 x, s16 y)
{
    return jj1_runtime_mask_cell(stage, x >> 2, y >> 2, 1);
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
            const Jj1EventInfo *info = jj1_event_info(stage, jj1_runtime_event(stage, bx, by));
            if (info->klass == JJ1_CLASS_SPRING) {
                if (springTopY) *springTopY = by << 5;
                return (u16)info->param * 21;
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
            if (jj1_event_info(stage, jj1_runtime_event(stage, bx, by))->klass
                == JJ1_CLASS_END) return 1;
    return 0;
}

void jj1_runtime_place_player(JazzGame *state, u8 stage)
{
    Jj1LevelData d = jj1_level_data(stage);
    s16 spawnY;
    s16 spawnX = ((s16)d.startX << 5) + 8;
    spawnY = ((s16)d.startY << 5) - 22;
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
