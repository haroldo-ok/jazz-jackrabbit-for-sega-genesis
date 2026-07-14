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
    JJ1_CLASS_DESTRUCT       /* behaviour 21: destructible block/sign */
};

/* Item modifiers mirroring the original meanings that matter here. */
enum {
    JJ1_ITEM_SCORE = 0,      /* gems, food: points only */
    JJ1_ITEM_HEALTH,         /* carrot: +1 energy */
    JJ1_ITEM_LIFE,           /* extra life */
    JJ1_ITEM_FASTFEET,       /* speed shoes (scored; effect TODO) */
    JJ1_ITEM_AMMO            /* weapon pickup (scored; weapon TODO) */
};

typedef struct {
    u8 klass;                /* JJ1_CLASS_* */
    u8 param;                /* item modifier, or spring launch magnitude */
    u8 points;               /* score / 25 to fit a byte (gem = 4 -> 100) */
    u8 strength;             /* enemy hit points, or destructible shots to break */
} Jj1EventInfo;

const Jj1EventInfo *jj1_event_info(u8 stage, u8 id);

#endif
