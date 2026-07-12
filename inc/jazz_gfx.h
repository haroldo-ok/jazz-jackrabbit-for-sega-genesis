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
#define T_ENEMY      (T_SPRING + (JJ1_SPRING_VARIANTS * JJ1_SPRING_TILES_PER_VARIANT))
#define T_GEM        (T_ENEMY + 4)
#define T_BULLET     (T_GEM + 1)
#define T_JJ1_SKY    (T_BULLET + 1)
#define T_JJ1_LEVEL0_SCREEN (T_JJ1_SKY + 8)
#define JJ1_LEVEL0_SCREEN_TILES (40 * 28)
/* Runtime cache: 11 blocks wide by 8 blocks high, 16 VDP tiles per block. */
#define T_JJ1_BLOCK_CACHE T_JJ1_LEVEL0_SCREEN
#define JJ1_BLOCK_CACHE_BLOCKS 88
#define JJ1_BLOCK_TILE_COUNT 16

extern const u32 jazz_world_tiles[56];
extern const u32 jazz_player_tiles[JJ1_PLAYER_FRAME_COUNT * JJ1_PLAYER_FRAME_TILES * 8];
extern const u32 jazz_spring_tiles[JJ1_SPRING_VARIANTS * JJ1_SPRING_TILES_PER_VARIANT * 8];
extern const u32 jazz_enemy_tiles[32];
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
