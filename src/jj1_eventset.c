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
#include "jj1_level3_eventset.inc"
#include "jj1_level4_eventset.inc"
#include "jj1_level5_eventset.inc"
#include "jj1_level6_eventset.inc"
#include "jj1_level7_eventset.inc"
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
/* Spring param is the absolute launch magnitude: the engine rises to
 * magnitude * 21 px above the spring block.  These provisional values are
 * derived from the converted masks: for each ID, the highest platform that
 * is actually landable from any of its placements (i.e. below the ceiling
 * over that spring), plus one step of margin.  Regenerated eventset includes
 * override these with the original per-level magnitudes. */
#define EV_SPRING(magnitude) { JJ1_CLASS_SPRING, (magnitude), 0, 0 }
#define EV_ONEWAY          { JJ1_CLASS_ONEWAY, 0, 0, 0 }
/* Destructible scenery (JJ1 behaviour 21): solid until it has taken `shots`
 * hits, then the block is swapped out and the space opens up.  The original
 * strength comes from the event record, so 1 is a provisional default. */
#define EV_DESTRUCT(shots) { JJ1_CLASS_DESTRUCT, 0, 0, (shots) }
#define EV_END             { JJ1_CLASS_END, 0, 0, 0 }

/* Diamondus set shared by LEVEL0.000 and LEVEL1.000. */
static const Jj1EventInfo jj1_eventset_diamondus[128] = {
    [1]  = EV_FLYER,     /* sparse, half airborne: hopping/flying enemy   */
    [2]  = EV_WALKER,    /* always grounded, spread across the level      */
    /* 3/4/5 lie in horizontal trails (66-100% have a same-ID neighbour) and
       never sit inside terrain: that is the shape of a pickup run (carrots,
       fast-fire, ammo), not a hazard strip.  They were classified as hazards,
       which is why collecting a carrot or a rapid-fire hurt.  Items never
       damage; the exact kind of each needs the original records, so they
       score for now. */
    [3]  = EV_FOOD,
    [4]  = EV_FOOD,
    [5]  = EV_FOOD,
    [6]  = EV_FLYER,     /* never grounded, isolated placements           */
    [7]  = EV_END,       /* single sign at the end of the route           */
    [8]  = EV_FLYER,
    [9]  = EV_WALKER,
    [10] = EV_WALKER,
    [11] = EV_WALKER,
    [13] = EV_CARROT,    /* grounded singles along the route              */
    [14] = EV_AMMO,      /* grounded, short rows                          */
    /* 15 is the breakable wall: it always appears as a horizontal PAIR of
       cells buried 100% inside solid rock (three pairs per level).  Shooting
       it opens the passage.  It is not a pickup - nothing embedded in rock
       could be collected. */
    [15] = EV_DESTRUCT(1),
    [16] = EV_FOOD,
    /* 17 sits on exactly one block in every level: the wooden "RABBITS STINK"
       signpost.  It is the destructible sign - it was classified as food, so
       shots passed straight through and the sign could never be cleared. */
    [17] = EV_DESTRUCT(1),
    [19] = EV_GEM,       /* grounded gem                                  */
    [20] = EV_GEM,       /* airborne gem rows/arcs                        */
    [21] = EV_SPRING(12),  /* clears its highest landable ledge: 224 px */
    [22] = EV_SPRING(12),  /* clears its highest landable ledge: 224 px */
    [23] = EV_SPRING(11),  /* clears its highest landable ledge: 192 px */
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
    /* 122 is one-way and 126 is spikes: the original hard-codes both of these
       at tile level (JJ1Level::checkMaskUp / checkSpikes).  Neither level uses
       126.  123/124/125 are region markers, NOT destructibles: they span
       hundreds of cells over ordinary terrain blocks and only 41-79% of them
       are even inside solid rock, so many sit in open air where a breakable
       block would be meaningless.  Treating them as destructible let the
       player shoot away arbitrary scenery. */
    [122] = EV_ONEWAY,
};

/* LEVEL2.000 set. */
static const Jj1EventInfo jj1_eventset_level2[128] = {
    [1]  = EV_WALKER,
    [8]  = EV_FLYER,
    [11] = EV_WALKER,
    [14] = EV_AMMO,
    [20] = EV_GEM,
    [23] = EV_SPRING(6),   /* clears its highest landable ledge:  96 px */
    [17] = EV_DESTRUCT(1),  /* wooden signpost, as in the Diamondus set */
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

/* Every level carries its own event records, so each stage gets its own table.
   Falling back to another level's table (as the three-stage version did for
   anything past stage 2) misclassifies every event in that level. */
static const Jj1EventInfo *const jj1_eventsets[JAZZ_STAGE_COUNT] = {
    jj1_level0_eventset, jj1_level1_eventset, jj1_level2_eventset,
    jj1_level3_eventset, jj1_level4_eventset, jj1_level5_eventset,
    jj1_level6_eventset, jj1_level7_eventset,
};

const Jj1EventInfo *jj1_event_info(u8 stage, u8 id)
{
    static const Jj1EventInfo none = { JJ1_CLASS_NONE, 0, 0, 0 };
    if (id >= 128) return &none;
    if (stage >= JAZZ_STAGE_COUNT) stage = 0;
    return &jj1_eventsets[stage][id];
}

#endif
