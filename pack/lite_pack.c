/* Compact pack of one episode (tools/build_cartridge.py): the stages of the
 * game pack, with tile pools, compressed maps and FM-only music. */
#include <genesis.h>
#include "jazz_gfx.h"
#include "jj1_events.h"
#include "jj1_pack.h"

#include "jj1_episode_includes.inc"
#include "jj1_episode_eventsets.inc"
#include "pack_game.inc"
#include "lite_pack.inc"

#define STAGE(n, world, mask) { \
    (const u32 *)lite_world##world##_pool, jj1_world##world##_palette, \
    jj1_world##world##_cycles, JJ1_WORLD##world##_CYCLES, mask, lite_level##n##_map, \
    jj1_level##n##_pal1, (const u32 *)lite_level##n##_player_pool, jj1_level##n##_player_states, \
    jj1_level##n##_spring_tiles, \
    (const u32 *)lite_level##n##_sprite_pool, jj1_level##n##_event_sprites, \
    &jj1_level##n##_bird_sprite, &jj1_level##n##_bird_left_sprite, \
    &jj1_level##n##_shield_sprite, \
    &jj1_level##n##_board_sprite, &jj1_level##n##_board_left_sprite, \
    &jj1_level##n##_boss_cannon_sprite, &jj1_level##n##_boss_cannon_left_sprite, \
    jj1_level##n##_boss_cannon_x, jj1_level##n##_boss_cannon_y, \
    jj1_level##n##_boss_cannon_left_x, jj1_level##n##_boss_cannon_left_y, \
    &jj1_level##n##_boss_late_sprite, &jj1_level##n##_boss_late_left_sprite, \
    &jj1_level##n##_boss_spike_sprite, &jj1_level##n##_boss_spike_left_sprite, \
    jj1_level##n##_boss_spike_x, jj1_level##n##_boss_spike_y, \
    jj1_level##n##_boss_spike_left_x, jj1_level##n##_boss_spike_left_y, \
    (const u16 *)lite_world##world##_index, (const u16 *)lite_level##n##_sprite_index, \
    (const u16 *)lite_level##n##_player_index }

/* LITE_LEVELS (lite_pack.inc): the level records point at the aPLib-packed
 * map and event grid. */

const Jj1GamePack pack_header __attribute__((section(".packhdr"), used)) = {
    JJ1_PACK_MAGIC_GAME,
    PACK_EPISODE,
    PACK_STAGES,
    {
#include "jj1_episode_end_scenes.inc"
    },
    PACK_STAGE_BONUS,
    PACK_STAGE_PLANET,
    {
#include "jj1_episode_stages.inc"
    },
    LITE_LEVELS,
    {
#include "jj1_episode_eventset_table.inc"
    },
    PACK_STAGE_MUSIC,
};
