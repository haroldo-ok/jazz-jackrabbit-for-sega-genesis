/* Game pack of one episode: the stages' art, maps and music.  Compiled once
 * per episode with that episode's directory (packs/ep<N>) on the include
 * path; see inc/jj1_pack.h for how the result is mapped. */
#include <genesis.h>
#include "jazz_gfx.h"
#include "jj1_events.h"
#include "jj1_pack.h"

/* Level art and world tile banks. */
#include "jj1_episode_includes.inc"
/* Per-level event records. */
#include "jj1_episode_eventsets.inc"
/* Music labels, level table and per-stage scene numbers. */
#include "pack_game.inc"

#define STAGE(n, world, mask) { \
    jj1_world##world##_block_tiles, jj1_world##world##_palette, \
    jj1_world##world##_cycles, JJ1_WORLD##world##_CYCLES, mask, jj1_level##n##_blocks, \
    jj1_level##n##_pal1, jj1_level##n##_player_tiles, jj1_level##n##_player_states, \
    jj1_level##n##_spring_tiles, \
    jj1_level##n##_event_sprite_tiles, jj1_level##n##_event_sprites, \
    &jj1_level##n##_bird_sprite, &jj1_level##n##_bird_left_sprite, \
    &jj1_level##n##_shield_sprite, \
    &jj1_level##n##_board_sprite, &jj1_level##n##_board_left_sprite, \
    &jj1_level##n##_boss_cannon_sprite, &jj1_level##n##_boss_cannon_left_sprite, \
    jj1_level##n##_boss_cannon_x, jj1_level##n##_boss_cannon_y, \
    jj1_level##n##_boss_cannon_left_x, jj1_level##n##_boss_cannon_left_y, \
    &jj1_level##n##_boss_late_sprite, &jj1_level##n##_boss_late_left_sprite, \
    &jj1_level##n##_boss_spike_sprite, &jj1_level##n##_boss_spike_left_sprite, \
    jj1_level##n##_boss_spike_x, jj1_level##n##_boss_spike_y, \
    jj1_level##n##_boss_spike_left_x, jj1_level##n##_boss_spike_left_y }

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
    PACK_LEVELS,
    {
#include "jj1_episode_eventset_table.inc"
    },
    PACK_STAGE_MUSIC,
};
