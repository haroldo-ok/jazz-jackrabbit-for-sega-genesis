#include "jj1_events.h"

/* If the importer has produced ground-truth event-set includes, use them.
 * Each generated include defines jj1_levelN_eventset[64] built from the
 * original level records, replacing the provisional tables below. */
#if defined(__has_include)
#if __has_include("jj1_level0_eventset.inc")
#define JJ1_HAVE_GENERATED_EVENTSETS 1
#include "jj1_level0_eventset.inc"
#include "jj1_level1_eventset.inc"
#include "jj1_level2_eventset.inc"
#endif
#endif

#ifndef JJ1_HAVE_GENERATED_EVENTSETS

/* Provisional classification of every event ID present in the converted
 * LEVEL0-2.000 grids.  Derived from the ID's placement statistics in the
 * original maps (ground contact, clustering, uniqueness, position) and the
 * globally fixed JJ1 IDs already verified in this port (122 one-way,
 * 21/22/23 springs).  Points are stored as score/25.
 *
 * ID 7 (levels 0/1) and ID 37 (level 2) are each a single on-ground event
 * near the end of the traversable map: the end-of-level signpost. */

#define EV_NONE            { JJ1_CLASS_NONE, 0, 0, 0 }
#define EV_GEM             { JJ1_CLASS_ITEM, JJ1_ITEM_SCORE, 4, 0 }   /* 100 */
#define EV_FOOD            { JJ1_CLASS_ITEM, JJ1_ITEM_SCORE, 2, 0 }   /*  50 */
#define EV_CARROT          { JJ1_CLASS_ITEM, JJ1_ITEM_HEALTH, 2, 0 }
#define EV_AMMO            { JJ1_CLASS_ITEM, JJ1_ITEM_AMMO, 2, 0 }
#define EV_LIFE            { JJ1_CLASS_ITEM, JJ1_ITEM_LIFE, 8, 0 }
#define EV_FEET            { JJ1_CLASS_ITEM, JJ1_ITEM_FASTFEET, 8, 0 }
#define EV_WALKER          { JJ1_CLASS_ENEMY_WALK, 0, 0, 1 }
#define EV_FLYER           { JJ1_CLASS_ENEMY_FLY, 0, 0, 1 }
#define EV_HAZARD          { JJ1_CLASS_HAZARD, 0, 0, 0 }
#define EV_SPRING(n)       { JJ1_CLASS_SPRING, (n), 0, 0 }
#define EV_ONEWAY          { JJ1_CLASS_ONEWAY, 0, 0, 0 }
#define EV_END             { JJ1_CLASS_END, 0, 0, 0 }

/* Diamondus set shared by LEVEL0.000 and LEVEL1.000. */
static const Jj1EventInfo jj1_eventset_diamondus[128] = {
    [1]  = EV_FLYER,     /* sparse, half airborne: hopping/flying enemy   */
    [2]  = EV_WALKER,    /* always grounded, spread across the level      */
    [3]  = EV_HAZARD,    /* clustered strips, mostly airborne             */
    [4]  = EV_HAZARD,
    [5]  = EV_HAZARD,
    [6]  = EV_FLYER,     /* never grounded, isolated placements           */
    [7]  = EV_END,       /* single sign at the end of the route           */
    [8]  = EV_FLYER,
    [9]  = EV_WALKER,
    [10] = EV_WALKER,
    [11] = EV_WALKER,
    [13] = EV_CARROT,    /* grounded singles along the route              */
    [14] = EV_AMMO,      /* grounded, short rows                          */
    [15] = EV_FOOD,
    [16] = EV_FOOD,
    [17] = EV_FOOD,
    [19] = EV_GEM,       /* grounded gem                                  */
    [20] = EV_GEM,       /* airborne gem rows/arcs                        */
    [21] = EV_SPRING(0),
    [22] = EV_SPRING(1),
    [23] = EV_SPRING(2),
    [24] = EV_LIFE,
    [25] = EV_FOOD,
    [26] = EV_FOOD,
    [27] = EV_FLYER,     /* hovers in mid-air                             */
    [28] = EV_LIFE,      /* unique per level                              */
    [29] = EV_NONE,      /* warp pair: TODO multiA/multiB targets         */
    [30] = EV_FEET,      /* unique per level                              */
    [33] = EV_FLYER,
    [34] = EV_FOOD,
    [35] = EV_WALKER,
    [122] = EV_ONEWAY,
    /* 123/124/125: large scenery regions in the original maps.           */
};

/* LEVEL2.000 set. */
static const Jj1EventInfo jj1_eventset_level2[128] = {
    [1]  = EV_WALKER,
    [8]  = EV_FLYER,
    [11] = EV_WALKER,
    [14] = EV_AMMO,
    [20] = EV_GEM,
    [23] = EV_SPRING(2),
    [24] = EV_LIFE,
    [28] = EV_FOOD,
    [35] = EV_FLYER,
    [36] = EV_FLYER,
    [37] = EV_END,       /* single grounded sign, deepest route point     */
    [38] = EV_FLYER,
    [122] = EV_ONEWAY,
};

const Jj1EventInfo *jj1_event_info(u8 stage, u8 id)
{
    static const Jj1EventInfo none = EV_NONE;
    const Jj1EventInfo *set = (stage >= 2) ? jj1_eventset_level2
                                           : jj1_eventset_diamondus;
    if (id >= 128) return &none;
    return &set[id];
}

#else /* JJ1_HAVE_GENERATED_EVENTSETS */

const Jj1EventInfo *jj1_event_info(u8 stage, u8 id)
{
    static const Jj1EventInfo none = { JJ1_CLASS_NONE, 0, 0, 0 };
    const Jj1EventInfo *set = jj1_level0_eventset;
    if (stage == 1) set = jj1_level1_eventset;
    else if (stage >= 2) set = jj1_level2_eventset;
    if (id >= 128) return &none;
    return &set[id];
}

#endif
