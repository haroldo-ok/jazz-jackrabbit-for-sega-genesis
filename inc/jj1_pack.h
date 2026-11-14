#ifndef JJ1_PACK_H
#define JJ1_PACK_H
/* Episode packs.
 *
 * The cartridge (5 MB, tools/build_cartridge.py) is laid out as:
 *
 *   $000000  the program (code, fonts, HUD, sounds, menus, bonus levels)
 *   then     one pack per episode: the seven stages' tile pools, sprites,
 *            packed maps and event grids, event tables and stage records,
 *            each linked to run at its own address
 *   $380000  the music, in ROM banks 7-9; jj1_music_start maps the track's
 *            bank there (SSF2 register $A130FF) before playing it.
 *
 * Each pack starts with a Jj1GamePack header. */
#include <genesis.h>
#include "jazz_gfx.h"
#include "jj1_events.h"
#include "jj1_scene_types.h"
#include "bonus.h"

#define JJ1_PACK_MAGIC_GAME  0x4A4A3147UL   /* "JJ1G" */
#define JJ1_PACK_STAGES      8
#define JJ1_PACK_BONUS       3

typedef struct {
    const u8 *blocks;
    const u8 *masks;
    const u8 *events;
    u16 startX, startY;
} Jj1PackLevel;

typedef struct {
    u32 magic;
    u8 episode;                         /* 1-based */
    u8 stages;
    u8 clearScene[JJ1_PACK_STAGES];     /* level-clear cutscene after the stage */
    u8 bonus[JJ1_PACK_STAGES];          /* which bonus level follows the stage */
    u8 planet[JJ1_PACK_STAGES];         /* world shown on the approach screen, 0xFF = none */
    Jj1StageArt art[JJ1_PACK_STAGES];
    Jj1PackLevel level[JJ1_PACK_STAGES];
    const Jj1EventInfo *eventset[JJ1_PACK_STAGES];
    const u8 *music[JJ1_PACK_STAGES];
} Jj1GamePack;

/* The header of the episode being played. */
extern const Jj1GamePack *jj1_game_pack;
#define JJ1_GAME_PACK jj1_game_pack

/* One entry per episode on the cartridge (lite_packdir.inc, written by
 * tools/build_cartridge.py once the packs have been linked). */
typedef struct {
    u8 stages;
    u8 number;                          /* 1-9, 10 = Holiday Hare */
    u32 gameAt;                         /* the pack's header */
    const char *title;                  /* "EPISODE 1" */
    const char *worlds;                 /* "DIAMONDUS TUBELECTRIC MEDIVO" */
} Jj1PackEntry;

extern const Jj1PackEntry jj1_pack_dir[];
extern const u8 jj1_pack_count;
extern u8 jj1_episode;                  /* 0-based index into jj1_pack_dir */

void jj1_pack_select(u8 episode);       /* choose the episode */
u8 jj1_pack_stages(void);

/* The stage art table of the mapped episode. */
#define jazz_stage_art (JJ1_GAME_PACK->art)
#endif
