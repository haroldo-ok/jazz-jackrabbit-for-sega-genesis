#ifndef JAZZ_GFX_H
#define JAZZ_GFX_H

#include <types.h>

#define T_EMPTY      (TILE_USER_INDEX + 0)
#define T_DIRT       (TILE_USER_INDEX + 1)
#define T_GRASS      (TILE_USER_INDEX + 2)
#define T_PLATFORM   (TILE_USER_INDEX + 3)
#define T_EXIT       (TILE_USER_INDEX + 4)
#define T_STAR       (TILE_USER_INDEX + 5)
#define T_HILL       (TILE_USER_INDEX + 6)
/* Eight converted 32x32 JJ1 walk frames, 16 VDP tiles each. */
#define JJ1_PLAYER_FRAME_COUNT 8
#define JJ1_PLAYER_FRAME_TILES 16
#define T_PLAYER     (TILE_USER_INDEX + 8)
#define T_SPRING     (T_PLAYER + (JJ1_PLAYER_FRAME_COUNT * JJ1_PLAYER_FRAME_TILES))
#define JJ1_SPRING_VARIANTS 3
#define JJ1_SPRING_TILES_PER_VARIANT 8
/* Enemy placeholders until the SPRITES.000 importer maps original frames:
 * 32x16 walker (2 frames), 16x16 flyer (2 frames), 16x16 static hazard. */
#define T_ENEMY_WALK (T_SPRING + (JJ1_SPRING_VARIANTS * JJ1_SPRING_TILES_PER_VARIANT))
#define JJ1_WALKER_FRAME_TILES 8
#define T_ENEMY_FLY  (T_ENEMY_WALK + (2 * JJ1_WALKER_FRAME_TILES))
#define JJ1_FLYER_FRAME_TILES 4
#define T_ENEMY_HAZARD (T_ENEMY_FLY + (2 * JJ1_FLYER_FRAME_TILES))
#define T_GEM        (T_ENEMY_HAZARD + 4)
#define T_BULLET     (T_GEM + 1)
#define T_JJ1_SKY    (T_BULLET + 1)
#define T_JJ1_LEVEL0_SCREEN (T_JJ1_SKY + 8)
#define JJ1_LEVEL0_SCREEN_TILES (40 * 28)
/* Runtime cache: keyed by block ID, so it must hold every distinct 32x32
 * block that can appear in the 11x8 visible window at once. Measured worst
 * case across the three shipped levels is 62 (level 1), so 72 slots leave
 * headroom for the "never evict a visible block" replacement policy.
 *
 * VRAM budget: SGDK puts the plane/window/scroll/sprite maps at 0xC000, so
 * only tiles 0..1535 exist and the 96-tile font sits at 1440..1535. Every
 * tile used here must stay below JJ1_VRAM_TILE_LIMIT or the streaming DMA
 * would overwrite the font and erase the HUD. */
#define JJ1_VRAM_TILE_LIMIT 1440
#define T_JJ1_BLOCK_CACHE T_JJ1_LEVEL0_SCREEN
#define JJ1_BLOCK_CACHE_BLOCKS 72
#define JJ1_BLOCK_TILE_COUNT 16

/* Fail the build rather than silently corrupting the font at runtime. */
typedef char jj1_block_cache_fits_in_vram[
    ((T_JJ1_BLOCK_CACHE + (JJ1_BLOCK_CACHE_BLOCKS * JJ1_BLOCK_TILE_COUNT))
     <= JJ1_VRAM_TILE_LIMIT) ? 1 : -1];

extern const u32 jazz_world_tiles[56];
extern const u32 jazz_player_tiles[JJ1_PLAYER_FRAME_COUNT * JJ1_PLAYER_FRAME_TILES * 8];
extern const u32 jazz_spring_tiles[JJ1_SPRING_VARIANTS * JJ1_SPRING_TILES_PER_VARIANT * 8];
extern const u32 jazz_enemy_walker_tiles[2 * JJ1_WALKER_FRAME_TILES * 8];
extern const u32 jazz_enemy_flyer_tiles[2 * JJ1_FLYER_FRAME_TILES * 8];
extern const u32 jazz_enemy_hazard_tiles[4 * 8];
extern const u32 jazz_gem_tile[8];
extern const u32 jazz_bullet_tile[8];
extern const u16 jazz_pal0[16];
extern const u16 jazz_pal1[16];
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
