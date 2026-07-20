#ifndef JJ1_EVENTS_H
#define JJ1_EVENTS_H

/* JJ1 event-set model.
 *
 * Original JJ1 levels carry a per-level event set: each grid event ID indexes
 * a record whose modifier/strength/points/movement bytes decide whether it is
 * an enemy, an item, a spring, a one-way platform, or the level exit (the
 * same classification OpenJazz applies: modifier 0 + strength = enemy,
 * points = item, modifier 6 = one-way, modifier 27 = end of level,
 * modifier 29 = upward spring).
 *
 * The converted data currently in the repository contains the full 256x64
 * event ID grids but not the event-set records, so src/jj1_eventset.c ships
 * provisional per-level tables classified from the original grids
 * (positions, ground contact, clustering).  Rerunning
 * tools/build_sgdk_jj1_level_data.py against an installed shareware copy
 * emits jj1_levelN_eventset.inc with the ground-truth records, which
 * override the provisional tables automatically.
 */

#ifdef JAZZ_HOST
#include <stdint.h>
#else
#include <types.h>
#endif

#include "jazz_game.h"

/* Behaviour classes used by the Genesis runtime. */
enum {
    JJ1_CLASS_NONE = 0,      /* scenery / unhandled */
    JJ1_CLASS_ITEM,          /* pickup scored on touch */
    JJ1_CLASS_ENEMY_WALK,    /* ground patroller (JJ1 movement 2/4) */
    JJ1_CLASS_ENEMY_FLY,     /* hovering enemy (JJ1 movement 6/7) */
    JJ1_CLASS_HAZARD,        /* static, hurts on touch, not shootable */
    JJ1_CLASS_SPRING,        /* modifier 29: upward spring */
    JJ1_CLASS_ONEWAY,        /* modifier 6: land-from-above platform */
    JJ1_CLASS_END,           /* modifier 27: end-of-level sign */
    JJ1_CLASS_DESTRUCT,      /* behaviour 21: destructible block/sign */
    JJ1_CLASS_TUBE,          /* movement 37/38: sucker tube, horizontal push */
    JJ1_CLASS_BRIDGE,        /* movement 28: walkable bridge span */
    JJ1_CLASS_UNBOARD        /* modifier 38: cancels airboard flight on touch */
};

/* Item modifiers mirroring the original meanings that matter here. */
enum {
    JJ1_ITEM_SCORE = 0,      /* gems, food: points only */
    JJ1_ITEM_HEALTH,         /* carrot: +1 energy */
    JJ1_ITEM_LIFE,           /* extra life */
    JJ1_ITEM_FASTFEET,       /* modifier 26: fast feet, higher run speed */
    JJ1_ITEM_AMMO,           /* weapon crate; shot type in the param high nibble */
    JJ1_ITEM_INVINCIBLE,     /* modifier 1: temporary invincibility */
    JJ1_ITEM_SHIELD,         /* modifiers 33/36: absorbs 1 or 4 hits */
    JJ1_ITEM_HIGHJUMP,       /* modifier 5: high-jump feet */
    JJ1_ITEM_BIRD,           /* modifier 34: bird companion */
    JJ1_ITEM_AIRBOARD        /* modifier 35: airboard flight */
};

typedef struct {
    u8 klass;                /* JJ1_CLASS_* */
    u8 param;                /* item kind, or spring/tube magnitude */
    u8 points;               /* score / 25 to fit a byte (gem = 4 -> 100) */
    /* Hit points.  For an item this is the crucial touch-vs-shoot flag: the
     * original only grabs a pickup on contact when strength is 0
     * (JJ1LevelPlayer::touchEvent's default case), otherwise the crate has to
     * be shot open and takeEvent grants the contents. */
    u8 strength;
} Jj1EventInfo;

const Jj1EventInfo *jj1_event_info(u8 stage, u8 id);

/* Test hook: apply a pickup's effect directly (declared here because it needs
 * the Jj1EventInfo type). */
void jazz_debug_grant(JazzGame *game, const Jj1EventInfo *info);

#endif
