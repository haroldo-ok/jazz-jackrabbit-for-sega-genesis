#ifndef JAZZ_GFX_H
#define JAZZ_GFX_H

#include <types.h>
#include "jazz_game.h"   /* JAZZ_MAX_ENEMIES: one VRAM sprite slot per enemy */

#define T_EMPTY      (TILE_USER_INDEX + 0)
#define T_DIRT       (TILE_USER_INDEX + 1)
#define T_GRASS      (TILE_USER_INDEX + 2)
#define T_PLATFORM   (TILE_USER_INDEX + 3)
#define T_EXIT       (TILE_USER_INDEX + 4)
#define T_STAR       (TILE_USER_INDEX + 5)
#define T_HILL       (TILE_USER_INDEX + 6)
/* Eight converted 32x32 JJ1 walk frames, 16 VDP tiles each. */
/* Jazz has a full animation state machine in the original (38 states, each a
 * left/right pair, selected from the level's MT_P_ANIMS block).  The port
 * drives the states it actually reaches; each is stored like the enemy sprites
 * (a cell of up to 2x2 chunks) and streamed into one VRAM slot by state.
 * The JJ1_PLAYER_* state values live in jazz_game.h so the game core, which
 * derives the state from physics, can name them without pulling in the VDP. */
/* Frames per state (kept short, like the enemy sets). */
#define JJ1_PLAYER_MAX_FRAMES 6
#define JJ1_PLAYER_FRAME_COUNT 8
/* One VRAM slot for Jazz, sized to the largest state cell (4x6 tiles), with
 * the current frame streamed in when the state or frame changes. */
#define JJ1_PLAYER_SLOT_TILES 24
#define T_PLAYER     (TILE_USER_INDEX + 8)
/* Jazz streams like the enemies: one slot, refilled when the frame changes.
 * Keeping all 8 frames resident cost 128 tiles that the enemy slots need. */
#define T_SPRING     (T_PLAYER + JJ1_PLAYER_SLOT_TILES)
#define JJ1_SPRING_VARIANTS 3
#define JJ1_SPRING_TILES_PER_VARIANT 8
/* Enemy VRAM.  Original JJ1 enemy frames are 32x32 (16 tiles each) and there
 * are far more of them than fit in VRAM, so each active enemy owns one slot
 * and the ROM streams its current frame in when the frame changes.  When no
 * original sprites have been extracted, the stand-in art lives in the same
 * region (it needs only the first 28 tiles). */
#define T_ENEMY_BASE (T_SPRING + (JJ1_SPRING_VARIANTS * JJ1_SPRING_TILES_PER_VARIANT))
/* JJ1 enemies are not 32x32: the Turtle Goon is 68x45, about twice Jazz's
 * size, so a slot must hold 8x6 tiles.  Keep in sync with the same constant
 * in tools/build_sgdk_jj1_visuals.py, which refuses to emit a frame too big
 * to fit here. */
#define JJ1_ENEMY_SLOT_TILES 48
#define JJ1_ENEMY_SLOTS JAZZ_MAX_ENEMIES
#define T_ENEMY_WALK T_ENEMY_BASE
#define JJ1_WALKER_FRAME_TILES 8
#define T_ENEMY_FLY  (T_ENEMY_WALK + (2 * JJ1_WALKER_FRAME_TILES))
#define JJ1_FLYER_FRAME_TILES 4
#define T_ENEMY_HAZARD (T_ENEMY_FLY + (2 * JJ1_FLYER_FRAME_TILES))
/* Uncollected items are drawn with their real extracted sprite, streamed into
 * a small keyed pool: only a few *distinct* item types are ever on screen at
 * once (a run of diskettes, a couple of carrots), so each distinct event id
 * gets a slot and the frame is uploaded once.  Slots are capped at 4x4 tiles;
 * the rare larger pickup draws its top-left 4x4.  This is what lets diskettes,
 * carrots and ammo crates look like themselves instead of a generic gem. */
#define JJ1_ITEM_SLOTS 2
#define JJ1_ITEM_SLOT_TILES 16
#define T_ITEM_BASE  (T_ENEMY_BASE + (JJ1_ENEMY_SLOTS * JJ1_ENEMY_SLOT_TILES))
/* The airboard has its own small slot so it can coexist with the shield and
 * bird, which occupy the two item slots. */
#define JJ1_BOARD_SLOT_TILES 12
#define T_BOARD      (T_ITEM_BASE + (JJ1_ITEM_SLOTS * JJ1_ITEM_SLOT_TILES))
#define T_GEM        (T_BOARD + JJ1_BOARD_SLOT_TILES)
#define T_BULLET     (T_GEM + 1)
#define T_JJ1_SKY    (T_BULLET + 1)
#define T_JJ1_LEVEL0_SCREEN (T_JJ1_SKY + 8)
#define JJ1_LEVEL0_SCREEN_TILES (40 * 28)
/* Runtime cache: keyed by block ID, so it must hold every distinct 32x32
 * block that can appear in the 11x8 visible window at once. Measured worst
 * case across the three shipped levels is 62 (level 1); 64 slots is the most
 * that still fits under the font once the enemy sprite slots are reserved.
 *
 * VRAM budget: SGDK puts the plane/window/scroll/sprite maps at 0xC000, so
 * only tiles 0..1535 exist and the 96-tile font sits at 1440..1535. Every
 * tile used here must stay below JJ1_VRAM_TILE_LIMIT or the streaming DMA
 * would overwrite the font and erase the HUD. */
#define JJ1_VRAM_TILE_LIMIT 1440
#define T_JJ1_BLOCK_CACHE T_JJ1_LEVEL0_SCREEN
#define JJ1_BLOCK_CACHE_BLOCKS 64
#define JJ1_BLOCK_TILE_COUNT 16

/* Fail the build rather than silently corrupting the font at runtime. */
typedef char jj1_block_cache_fits_in_vram[
    ((T_JJ1_BLOCK_CACHE + (JJ1_BLOCK_CACHE_BLOCKS * JJ1_BLOCK_TILE_COUNT))
     <= JJ1_VRAM_TILE_LIMIT) ? 1 : -1];

extern const u32 jazz_world_tiles[56];
extern const u32 jazz_enemy_walker_tiles[2 * JJ1_WALKER_FRAME_TILES * 8];
extern const u32 jazz_enemy_flyer_tiles[2 * JJ1_FLYER_FRAME_TILES * 8];
extern const u32 jazz_enemy_hazard_tiles[4 * 8];
extern const u32 jazz_world_tiles[7 * 8];

/* One entry per event id: where its frames live in jazz_event_sprite_tiles
 * (in tiles) and how many there are.  Populated only when original sprites
 * have been extracted; jazz_event_sprites is NULL otherwise and the ROM falls
 * back to the stand-in art above. */
typedef struct {
    u16 tile;      /* first tile of the right-facing set */
    u8 frames;
    u8 tilesW;     /* cell size; frames are stored as up to 2x2 chunks of  */
    u8 tilesH;     /* at most 4x4 tiles, one hardware sprite per chunk     */
    u16 tileLeft;  /* first tile of the left-facing set (== tile if none): */
                   /* JJ1 has separate art per direction, picked not flipped */
} Jj1EventSprite;

/* Everything that changes between stages.  The shareware ships eight levels
 * across three worlds: the block tile bank and its palette belong to the
 * world, while the map, sprite palette, Jazz frames, springs and event sprites
 * are all per level (each level carries its own animation table). */
typedef struct {
    const u32 *blockTiles;      /* shared by every level in the world */
    const u16 *blockPalette;
    const u8  *blocks;
    const u16 *pal1;            /* sprite palette */
    const u32 *playerTiles;
    const Jj1EventSprite *playerStates;   /* one cell per JJ1_PLAYER_* state */
    const u32 *springTiles;
    const u32 *spriteTiles;
    const Jj1EventSprite *sprites;
    const Jj1EventSprite *bird;       /* companion, right-facing */
    const Jj1EventSprite *birdLeft;
    const Jj1EventSprite *shield;     /* orbiting shield orb */
    const Jj1EventSprite *board;      /* airboard, drawn under the rider */
    const Jj1EventSprite *boardLeft;
} Jj1StageArt;

extern const Jj1StageArt jazz_stage_art[JAZZ_STAGE_COUNT];
extern const u32 jazz_gem_tile[8];
extern const u32 jazz_bullet_tile[8];
extern const u16 jazz_pal0[16];
extern const u16 jazz_pal2[16];
extern const u16 jazz_pal3[16];
extern const u32 jj1_sky_tiles[64];
extern const u16 jj1_sky_palette[16];
extern const u32 jj1_level0_screen_tiles[JJ1_LEVEL0_SCREEN_TILES * 8];
extern const u16 jj1_level0_screen_palette[16];
extern const u32 jj1_level0_block_tiles[240 * JJ1_BLOCK_TILE_COUNT * 8];
extern const u8 jj1_level0_blocks[256 * 64];
extern const u8 jj1_level0_events[256 * 64];
extern const u8 jj1_level0_masks[(240 + 16) * 8];
extern const u16 jj1_level0_start_x, jj1_level0_start_y;
extern const u16 jj1_level0_palette[16];
extern const u32 jj1_level1_block_tiles[240 * JJ1_BLOCK_TILE_COUNT * 8];
extern const u8 jj1_level1_blocks[256 * 64];
extern const u8 jj1_level1_events[256 * 64];
extern const u8 jj1_level1_masks[(240 + 16) * 8];
extern const u16 jj1_level1_start_x, jj1_level1_start_y;
extern const u16 jj1_level1_palette[16];
extern const u32 jj1_level2_block_tiles[240 * JJ1_BLOCK_TILE_COUNT * 8];
extern const u8 jj1_level2_blocks[256 * 64];
extern const u8 jj1_level2_events[256 * 64];
extern const u8 jj1_level2_masks[(240 + 16) * 8];
extern const u16 jj1_level2_start_x, jj1_level2_start_y;
extern const u16 jj1_level2_palette[16];
extern const u32 jj1_level1_screen_tiles[JJ1_LEVEL0_SCREEN_TILES * 8];
extern const u16 jj1_level1_screen_palette[16];
extern const u32 jj1_level2_screen_tiles[JJ1_LEVEL0_SCREEN_TILES * 8];
extern const u16 jj1_level2_screen_palette[16];

#endif
