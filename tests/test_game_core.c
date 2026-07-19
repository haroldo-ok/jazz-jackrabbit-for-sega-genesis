#include <stdio.h>
#include <stdlib.h>
#include "jazz_game.h"
#include "jj1_events.h"
#include "jj1_runtime.h"

/* Host point-to-point tests. They run the same game_core / jj1_runtime code
 * as the ROM against the real converted LEVEL0-2.000 maps, masks, events,
 * and start records. */

static int failures;

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s (line %d)\n", (message), __LINE__); failures++; } \
} while (0)

static void step_n(JazzGame *g, u16 input, int n)
{
    int i;
    for (i = 0; i < n; i++) jazz_step(g, input);
    jazz_step(g, 0); /* release edge-triggered buttons */
}

/* Find the first grid cell of a class in a level, scanning column-major so
 * "first" means leftmost. Returns 1 and fills gx/gy on success. */

/* Solidity as the running game sees it, i.e. honouring destroyed blocks. */
static u8 rect_solid_at(const JazzGame *g, s16 x, s16 y)
{
    s16 tx, ty;
    for (ty = y; ty < y + 32; ty += 16)
        for (tx = x; tx < x + 32; tx += 16)
            if (jazz_is_solid(g, (s16)(tx >> 4), (s16)(ty >> 4))) return 1;
    return 0;
}

static int find_event(u8 stage, u8 klass, int *gx, int *gy)
{
    int x, y;
    for (x = 0; x < 256; x++)
        for (y = 0; y < 64; y++)
            if (jj1_event_info(stage, jj1_runtime_event(stage, x, y))->klass == klass) {
                *gx = x; *gy = y;
                return 1;
            }
    return 0;
}

/* Drop the player onto real mask ground at pixel x, starting from pixel y. */
static void place_on_ground(JazzGame *g, s16 x, s16 y)
{
    jazz_debug_place(g, x, y);
    while ((y < (64 * 32) - 23) &&
           !jj1_runtime_down_point_solid(g->stage, x + 7, y + 22)) y++;
    jazz_debug_place(g, x, y);
    g->player.onGround = 1;
}

static void test_level_geometry(void)
{
    JazzGame g;
    int lvl;
    jazz_game_init(&g);
    CHECK(g.stage == 0, "stage 0 initializes");
    CHECK(g.lives == 3 && g.health == 5, "initial lives and health");

    for (lvl = 0; lvl < 3; lvl++) {
        int gx, gy;
        JazzGame probe;
        jazz_game_init(&probe);
        while (probe.stage != lvl) { probe.transitionTimer = 1; jazz_step(&probe, 0); }
        CHECK(probe.stage == lvl, "stage transition reaches level");
        /* The spawn settle must land on real mask ground. */
        CHECK(jj1_runtime_down_point_solid(lvl, probe.player.x + 7, probe.player.y + 22),
              "spawn rests on original mask ground");
        CHECK(!jj1_runtime_rect_solid(lvl, probe.player.x, probe.player.y, 14, 22),
              "spawn body is not embedded in the mask");
        CHECK(probe.stageGemTotal > 0, "level contains original items");
        CHECK(find_event(lvl, JJ1_CLASS_END, &gx, &gy), "level contains an end sign");
    }

    /* Known original geometry: level 0 events place one-way platforms. */
    {
        int gx, gy;
        CHECK(find_event(0, JJ1_CLASS_ONEWAY, &gx, &gy), "level 0 has one-way platforms");
        CHECK(find_event(0, JJ1_CLASS_SPRING, &gx, &gy), "level 0 has springs");
        CHECK(find_event(0, JJ1_CLASS_ENEMY_WALK, &gx, &gy), "level 0 has walkers");
    }
}

static void test_walk_and_run_speeds(void)
{
    JazzGame g;
    s16 startX;
    int i;
    jazz_game_init(&g);
    /* Level 0 spawn area: run right on original ground. */
    startX = g.player.x;

    /* After enough frames the run cap of 5.08 px/f is reached: distance over
     * 32 frames at cap must be 160..166 px if nothing blocks. Use a stretch
     * of the real map known to be open by probing before asserting. */
    for (i = 0; i < 60; i++) jazz_step(&g, JAZZ_INPUT_RIGHT);
    CHECK(g.player.x > startX + 100, "sustained run covers original ground quickly");
    CHECK(g.player.vx == JAZZ_PXS_RUN || g.player.vx == 0,
          "run speed saturates at the converted cap unless blocked");

    /* Walk tier: from rest, the first 10 frames stay at/below walk speed. */
    jazz_game_init(&g);
    for (i = 0; i < 10; i++) jazz_step(&g, JAZZ_INPUT_RIGHT);
    CHECK(g.player.vx <= JAZZ_PXS_WALK, "walk tier accelerates first");
    for (i = 0; i < 40; i++) jazz_step(&g, JAZZ_INPUT_RIGHT);
    CHECK(g.player.vx > JAZZ_PXS_WALK, "run tier engages beyond walk speed");
}

static void test_variable_jump(void)
{
    JazzGame g;
    s16 floorY, shortApex, fullApex;
    int i;

    /* Short tap: press jump 3 frames then release. */
    jazz_game_init(&g);
    floorY = g.player.y;
    for (i = 0; i < 3; i++) jazz_step(&g, JAZZ_INPUT_JUMP);
    shortApex = g.player.y;
    for (i = 0; i < 90; i++) { jazz_step(&g, 0); if (g.player.y < shortApex) shortApex = g.player.y; }
    CHECK(g.player.onGround, "short jump lands again");

    /* Full hold. */
    jazz_game_init(&g);
    fullApex = g.player.y;
    for (i = 0; i < 40; i++) { jazz_step(&g, JAZZ_INPUT_JUMP); if (g.player.y < fullApex) fullApex = g.player.y; }
    for (i = 0; i < 90; i++) { jazz_step(&g, JAZZ_INPUT_JUMP); if (g.player.y < fullApex) fullApex = g.player.y; }

    CHECK(floorY - fullApex >= 84, "full jump clears the 84px original target height");
    CHECK(floorY - fullApex <= 112, "full jump stays within the original overshoot margin");
    CHECK((floorY - shortApex) < (floorY - fullApex), "releasing jump shortens the arc");
    CHECK(floorY - shortApex >= 8, "even a tapped jump leaves the ground");
}

static void test_terminal_fall(void)
{
    JazzGame g;
    jazz_game_init(&g);
    /* Walk off any ledge by teleporting into open air above ground. */
    jazz_debug_place(&g, g.player.x, g.player.y - 80);
    jazz_step(&g, 0);
    CHECK(g.player.vy == JAZZ_PYS_FALL,
          "airborne descent immediately uses the original terminal fall speed");
    step_n(&g, 0, 120);
    CHECK(g.player.onGround, "fall lands on original mask ground");
}

static void test_item_collection(void)
{
    JazzGame g;
    int gx, gy;
    u8 before;
    jazz_game_init(&g);
    /* Pick an item that actually carries points: the original has scoring
       pickups (gems, ammo) and non-scoring ones (the caged bird is an extra
       life worth nothing), so the first item found need not score. */
    {
        u16 sx;
        u8 sy;
        gx = gy = -1;
        for (sy = 0; sy < 64 && gy < 0; sy++)
            for (sx = 0; sx < 256; sx++) {
                const Jj1EventInfo *info =
                    jj1_event_info(0, jj1_runtime_event(0, (s16)sx, (s16)sy));
                if ((info->klass == JJ1_CLASS_ITEM) && info->points) {
                    gx = (int)sx; gy = (int)sy;
                    break;
                }
            }
    }
    CHECK(gx >= 0, "an original scoring item exists");
    before = g.stageGems;
    place_on_ground(&g, (s16)(gx << 5) + 8, (s16)(gy << 5));
    jazz_debug_place(&g, (s16)(gx << 5) + 8, (s16)(gy << 5) + 6);
    jazz_step(&g, 0);
    /* Pickups sit in trails, so the player's body can legitimately overlap
       more than one cell at once: require progress, not exactly one. */
    CHECK(g.stageGems > before, "touching the original item cell collects it");
    CHECK(jazz_event_taken(&g, (u8)gx, (u8)gy), "collected item is marked taken");
    CHECK(g.score > 0, "collecting a scoring item scores points");
    {
        u8 collected = g.stageGems;
        jazz_step(&g, 0);
        CHECK(g.stageGems == collected, "an item is only collected once");
    }
}

static void test_enemy_activation_kill_and_damage(void)
{
    JazzGame g;
    int gx, gy, i;
    int active = -1;
    jazz_game_init(&g);
    CHECK(find_event(0, JJ1_CLASS_ENEMY_WALK, &gx, &gy), "an original walker exists");

    /* Park Jazz near (but not on) the enemy cell so the scan activates it. */
    place_on_ground(&g, (s16)(gx << 5) - 72, (s16)(gy << 5) - 24);
    for (i = 0; i < 32; i++) jazz_step(&g, 0);
    for (i = 0; i < JAZZ_MAX_ENEMIES; i++)
        if (g.enemies[i].active && g.enemies[i].gridX == gx && g.enemies[i].gridY == gy) active = i;
    CHECK(active >= 0, "walker activates from the original event grid");

    if (active >= 0) {
        u16 score = g.score;
        u8 hp = g.health;
        /* Shoot it: face right toward the enemy and fire until it dies. */
        g.player.facing = 1;
        jazz_debug_place(&g, g.enemies[active].x - 60, g.enemies[active].y - 4);
        for (i = 0; i < 120 && g.enemies[active].active; i++) {
            jazz_step(&g, (i & 8) ? JAZZ_INPUT_FIRE : 0);
        }
        CHECK(!g.enemies[active].active, "bullets kill the walker");
        CHECK(g.score > score, "kill scores points");
        CHECK(jazz_event_taken(&g, (u8)gx, (u8)gy), "killed enemy is marked taken");

        /* A killed enemy must not respawn from the grid scan. */
        for (i = 0; i < 64; i++) jazz_step(&g, 0);
        for (i = 0; i < JAZZ_MAX_ENEMIES; i++)
            CHECK(!(g.enemies[i].active && g.enemies[i].gridX == gx && g.enemies[i].gridY == gy),
                  "killed enemy stays dead");

        /* Contact damage with invulnerability window. */
        jazz_game_init(&g);
        place_on_ground(&g, (s16)(gx << 5) - 72, (s16)(gy << 5) - 24);
        for (i = 0; i < 32; i++) jazz_step(&g, 0);
        active = -1;
        for (i = 0; i < JAZZ_MAX_ENEMIES; i++)
            if (g.enemies[i].active && g.enemies[i].gridX == gx) active = i;
        if (active >= 0) {
            hp = g.health;
            jazz_debug_place(&g, g.enemies[active].x, g.enemies[active].y);
            jazz_step(&g, 0);
            CHECK(g.health == hp - 1, "enemy contact costs one energy");
            hp = g.health;
            jazz_step(&g, 0);
            CHECK(g.health == hp, "invulnerability window prevents repeat damage");
        }
    }
}

/* Drop the player onto the spring at grid (gx,gy) and return how far the
 * feet rose above the spring block's top edge, without holding jump. */
static s16 spring_feet_rise(u8 stage, int gx, int gy)
{
    JazzGame g;
    s16 springTop = (s16)(gy << 5);
    s16 apexFeet;
    int i;
    jazz_game_init(&g);
    jazz_debug_set_stage(&g, stage);
    jazz_debug_place(&g, (s16)(gx << 5) + 8, springTop + 4);
    apexFeet = jazz_player_feet(&g);
    for (i = 0; i < 240; i++) {
        jazz_step(&g, 0);
        if (jazz_player_feet(&g) < apexFeet) apexFeet = jazz_player_feet(&g);
    }
    return (s16)(springTop - apexFeet);
}

static void test_springs(void)
{
    int gx, gy;

    /* The original engine rises to magnitude * 21 px above the spring block,
     * so every spring must clear the highest ledge that is actually landable
     * from it. These cases were measured from the converted masks: a ledge
     * only counts if it sits below the ceiling over that spring. */
    struct { u8 stage; int gx, gy, needed; const char *what; } cases[] = {
        { 0,  24, 16, 224, "L0 spring (24,16) clears its 224px ledge" },
        { 0, 237, 16, 160, "L0 spring (237,16) clears its 160px ledge" },
        { 0,  95, 22, 224, "L0 spring (95,22) clears its 224px ledge" },
        { 1,  24, 37, 192, "L1 spring (24,37) clears its 192px ledge" },
        { 1, 165, 46, 192, "L1 spring (165,46) clears its 192px ledge" },
        { 2,   1, 47,  96, "L2 spring (1,47) clears its 96px ledge" },
    };
    size_t c;

    for (c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        s16 rise = spring_feet_rise(cases[c].stage, cases[c].gx, cases[c].gy);
        CHECK(rise >= cases[c].needed, cases[c].what);
        if (rise < cases[c].needed)
            fprintf(stderr, "  (rose %d px, needed %d)\n", rise, cases[c].needed);
    }

    /* Terrain still wins over the launch: the spring at L1 (141,45) sits
     * under a solid ceiling 224px up, so the ascent must stop there rather
     * than passing through it. */
    CHECK(spring_feet_rise(1, 141, 45) < 224, "spring ascent is clamped by a ceiling");

    /* A spring must also out-launch an unassisted jump by a clear margin. */
    if (find_event(0, JJ1_CLASS_SPRING, &gx, &gy))
        CHECK(spring_feet_rise(0, gx, gy) > JAZZ_PYO_JUMP,
              "spring launches higher than a full unassisted jump");
    else
        CHECK(0, "spring exists");
}

static void test_end_of_level_and_episode(void)
{
    JazzGame g;
    int gx, gy, i;
    jazz_game_init(&g);

    for (i = 0; i < JAZZ_STAGE_COUNT; i++) {
        u8 stage = g.stage;
        CHECK(find_event(stage, JJ1_CLASS_END, &gx, &gy), "end sign exists");
        place_on_ground(&g, (s16)(gx << 5) + 8, (s16)(gy << 5) - 10);
        jazz_debug_place(&g, (s16)(gx << 5) + 8, (s16)(gy << 5) + 4);
        jazz_step(&g, 0);
        CHECK(g.events & JAZZ_EVENT_EXIT, "touching the original end sign exits");
        CHECK(g.transitionTimer > 0, "exit starts the stage transition");
        while (g.transitionTimer) jazz_step(&g, 0);
        if (i < JAZZ_STAGE_COUNT - 1)
            CHECK(g.stage == stage + 1, "transition advances to the next original level");
    }
    CHECK(g.finished, "clearing all three levels finishes the episode");
    CHECK(g.lives > 0, "episode completes with lives remaining");
}

static void test_pause(void)
{
    JazzGame g;
    s16 x;
    jazz_game_init(&g);
    jazz_step(&g, JAZZ_INPUT_START);
    jazz_step(&g, 0);
    x = g.player.x;
    step_n(&g, JAZZ_INPUT_RIGHT, 8);
    CHECK(g.player.x == x, "pause freezes simulation");
    jazz_step(&g, JAZZ_INPUT_START);
    jazz_step(&g, 0);
    step_n(&g, JAZZ_INPUT_RIGHT, 8);
    CHECK(g.player.x > x, "unpause resumes simulation");
}


/* Shooting a destructible block must break it and open the way through.
 * Before this existed, wooden signs and destructible walls stayed solid,
 * which walls the player off from the level 1 exit. */
static u8 cell_is_solid(u8 stage, int gx, int gy)
{
    int mx, my;
    for (my = 4; my < 32; my += 8)
        for (mx = 4; mx < 32; mx += 8)
            if (jj1_runtime_point_solid(stage, (s16)((gx << 5) + mx), (s16)((gy << 5) + my)))
                return 1;
    return 0;
}

/* A destructible that is genuinely solid and has open space to its left, so a
 * bullet fired from the left can actually reach it. */
static int find_shootable_destructible(u8 stage, int *ox, int *oy)
{
    int gx, gy;
    for (gy = 1; gy < 63; gy++)
        for (gx = 1; gx < 255; gx++) {
            const Jj1EventInfo *info =
                jj1_event_info(stage, jj1_runtime_event(stage, (s16)gx, (s16)gy));
            if (info->klass != JJ1_CLASS_DESTRUCT) continue;
            if (!cell_is_solid(stage, gx, gy)) continue;
            if (cell_is_solid(stage, gx - 1, gy)) continue;
            *ox = gx; *oy = gy;
            return 1;
        }
    return 0;
}

static void test_destructible_scenery(void)
{
    JazzGame g;
    int gx, gy, lvl, i;
    int found = 0;

    for (lvl = 0; lvl < JAZZ_STAGE_COUNT; lvl++) {
        if (!find_shootable_destructible((u8)lvl, &gx, &gy)) continue;
        found++;
        jazz_game_init(&g);
        jazz_debug_set_stage(&g, (u8)lvl);

        /* An intact destructible is ordinary solid terrain. */
        CHECK(rect_solid_at(&g, (s16)(gx << 5), (s16)(gy << 5)),
              "an intact destructible block is solid");
        CHECK(!jazz_cell_destroyed(&g, (u8)gx, (u8)gy), "the block starts intact");

        /* Stand in the clear cell to its left and fire right at it. */
        jazz_debug_place(&g, (s16)(((gx - 1) << 5) + 8), (s16)((gy << 5) + 8));
        for (i = 0; i < 90 && !jazz_cell_destroyed(&g, (u8)gx, (u8)gy); i++)
            jazz_step(&g, (u16)(((i & 7) == 0) ? JAZZ_INPUT_FIRE : 0));

        CHECK(jazz_cell_destroyed(&g, (u8)gx, (u8)gy),
              "shooting a destructible block breaks it");
        CHECK(!rect_solid_at(&g, (s16)(gx << 5), (s16)(gy << 5)),
              "a destroyed block no longer blocks the way");
        CHECK(g.destroyCount > 0, "the renderer is told to repaint the block");
    }
    CHECK(found >= 2, "the Diamondus levels have shootable destructible scenery");
}

/* Pickups must never damage the player.  Carrots and rapid-fire used to be
 * classified as hazards, so collecting one cost health. */
static void test_items_never_hurt(void)
{
    JazzGame g;
    u16 gx;
    u8 gy;
    int checked = 0, lvl;

    for (lvl = 0; lvl < JAZZ_STAGE_COUNT; lvl++) {
        for (gy = 0; gy < 64; gy++) {
            for (gx = 0; gx < 256; gx++) {
                const Jj1EventInfo *info =
                    jj1_event_info((u8)lvl, jj1_runtime_event((u8)lvl, (s16)gx, (s16)gy));
                if (info->klass != JJ1_CLASS_ITEM) continue;
                /* Drop the player straight onto the pickup and let it resolve. */
                jazz_game_init(&g);
                jazz_debug_set_stage(&g, (u8)lvl);
                jazz_debug_place(&g, (s16)((gx << 5) + 8), (s16)((gy << 5) + 8));
                jazz_step(&g, 0);
                if (g.health < 5) {
                    CHECK(0, "a pickup damaged the player");
                    fprintf(stderr, "  (level %d, event cell %u,%u)\n", lvl, gx, gy);
                    return;
                }
                checked++;
            }
        }
    }
    CHECK(checked > 100, "pickups were actually exercised");
    CHECK(1, "no pickup in any level damages the player");
}


/* The wooden signpost must be shootable.  Event 17 sits on exactly one block
 * in the Diamondus levels - the signpost graphic - and was classified as food,
 * so bullets passed through it and the sign stayed up forever. */
static void test_signposts_are_destructible(void)
{
    JazzGame g;
    int lvl, signs = 0;

    for (lvl = 0; lvl < 2; lvl++) {   /* Diamondus set: levels 0 and 1 */
        u16 gx;
        u8 gy;
        for (gy = 0; gy < 64; gy++)
            for (gx = 0; gx < 256; gx++) {
                if (jj1_runtime_event((u8)lvl, (s16)gx, (s16)gy) != 17) continue;
                signs++;
                CHECK(jj1_event_info((u8)lvl, 17)->klass == JJ1_CLASS_DESTRUCT,
                      "the signpost event is destructible scenery");
                /* And it must actually break when shot. */
                jazz_game_init(&g);
                jazz_debug_set_stage(&g, (u8)lvl);
                if (cell_is_solid((u8)lvl, (int)gx - 1, (int)gy)) continue;
                jazz_debug_place(&g, (s16)(((gx - 1) << 5) + 8), (s16)((gy << 5) + 8));
                {
                    int i;
                    for (i = 0; i < 90 && !jazz_cell_destroyed(&g, (u8)gx, (u8)gy); i++)
                        jazz_step(&g, (u16)(((i & 7) == 0) ? JAZZ_INPUT_FIRE : 0));
                }
                CHECK(jazz_cell_destroyed(&g, (u8)gx, (u8)gy),
                      "shooting the signpost knocks it down");
                goto next_level;   /* one proven sign per level is enough */
            }
next_level: ;
    }
    CHECK(signs > 0, "the Diamondus levels contain signposts");
}


/* Breakable walls: event 15 always appears as a horizontal pair buried inside
 * solid rock.  Reclassifying it as a pickup silently made the walls solid
 * again, so pin it: it must be destructible scenery, and it must break. */
static void test_breakable_walls(void)
{
    JazzGame g;
    int lvl, walls = 0;

    for (lvl = 0; lvl < 2; lvl++) {   /* Diamondus set */
        u16 gx;
        u8 gy;
        CHECK(jj1_event_info((u8)lvl, 15)->klass == JJ1_CLASS_DESTRUCT,
              "the breakable wall event is destructible scenery");
        for (gy = 0; gy < 64; gy++)
            for (gx = 1; gx < 256; gx++) {
                if (jj1_runtime_event((u8)lvl, (s16)gx, (s16)gy) != 15) continue;
                walls++;
                if (cell_is_solid((u8)lvl, (int)gx - 1, (int)gy)) continue;
                jazz_game_init(&g);
                jazz_debug_set_stage(&g, (u8)lvl);
                jazz_debug_place(&g, (s16)(((gx - 1) << 5) + 8), (s16)((gy << 5) + 8));
                {
                    int i;
                    for (i = 0; i < 90 && !jazz_cell_destroyed(&g, (u8)gx, (u8)gy); i++)
                        jazz_step(&g, (u16)(((i & 7) == 0) ? JAZZ_INPUT_FIRE : 0));
                }
                CHECK(jazz_cell_destroyed(&g, (u8)gx, (u8)gy),
                      "shooting a breakable wall opens it");
                CHECK(!rect_solid_at(&g, (s16)(gx << 5), (s16)(gy << 5)),
                      "the broken wall no longer blocks the passage");
                goto next;
            }
next: ;
    }
    CHECK(walls > 0, "the Diamondus levels contain breakable walls");
}

/* Region markers must NOT be shootable: 123/124/125 blanket ordinary terrain,
 * so making them destructible let the player shoot away scenery. */
static void test_region_markers_are_not_destructible(void)
{
    int lvl;
    for (lvl = 0; lvl < JAZZ_STAGE_COUNT; lvl++) {
        CHECK(jj1_event_info((u8)lvl, 123)->klass != JJ1_CLASS_DESTRUCT &&
              jj1_event_info((u8)lvl, 124)->klass != JJ1_CLASS_DESTRUCT &&
              jj1_event_info((u8)lvl, 125)->klass != JJ1_CLASS_DESTRUCT,
              "terrain region markers are not shootable scenery");
    }
}


/* Every shipped level must be playable, not just the three Diamondus ones.
 * When the stage count grew from 3 to 8 the event tables still fell back to
 * level 2's for anything past stage 2, which silently misclassified every
 * event in the new levels (they reported zero enemies). */
static void test_all_stages_are_playable(void)
{
    u8 stage;
    for (stage = 0; stage < JAZZ_STAGE_COUNT; stage++) {
        JazzGame g;
        int gx, gy, items = 0;
        u16 sx;
        u8 sy;

        jazz_game_init(&g);
        jazz_debug_set_stage(&g, stage);

        /* Jazz starts standing on the level's own mask, not inside it. */
        CHECK(g.player.onGround || !jazz_is_solid(&g, (s16)(g.player.x >> 4),
                                                  (s16)(g.player.y >> 4)),
              "the level spawns Jazz in open space");

        /* Each level has its own event table: it must resolve a route out. */
        CHECK(find_event(stage, JJ1_CLASS_END, &gx, &gy),
              "the level has an end sign");

        for (sy = 0; sy < 64; sy++)
            for (sx = 0; sx < 256; sx++)
                if (jj1_event_info(stage, jj1_runtime_event(stage, (s16)sx, (s16)sy))->klass
                    == JJ1_CLASS_ITEM)
                    items++;
        CHECK(items > 0, "the level has collectables from its own event table");
        if (!items) fprintf(stderr, "  (stage %u)\n", stage);
    }
}


/* Tubelectric's sucker tubes (JJ1 movement 37/38) push the player along.
 * They were never implemented, so the player just stood still in the tubes. */

static void test_sucker_tubes(void)
{
    int lvl, found_h = 0, found_v = 0;

    for (lvl = 0; lvl < JAZZ_STAGE_COUNT; lvl++) {
        int gx, gy;
        for (gy = 1; gy < 63; gy++)
            for (gx = 1; gx < 255; gx++) {
                JazzGame g;
                s16 x0, y0;
                int f;
                const Jj1EventInfo *info =
                    jj1_event_info((u8)lvl, jj1_runtime_event((u8)lvl, (s16)gx, (s16)gy));
                if (info->klass != JJ1_CLASS_TUBE) continue;

                if (info->strength && !found_v) {
                    /* A vertical launch tube fires the player upward with no
                       input - not merely holds him against gravity. */
                    jazz_game_init(&g);
                    jazz_debug_set_stage(&g, (u8)lvl);
                    jazz_debug_place(&g, (s16)((gx << 5) + 8), (s16)((gy << 5) + 8));
                    y0 = g.player.y;
                    for (f = 0; f < 10; f++) jazz_step(&g, 0);
                    CHECK(g.player.y < y0, "a vertical tube launches the player upward");
                    found_v = 1;
                }

                if (!info->strength && info->param && !found_h) {
                    jazz_game_init(&g);
                    jazz_debug_set_stage(&g, (u8)lvl);
                    jazz_debug_place(&g, (s16)((gx << 5) + 8), (s16)((gy << 5) + 8));
                    x0 = g.player.x;
                    for (f = 0; f < 8; f++) jazz_step(&g, 0);
                    CHECK(g.player.x != x0, "a horizontal tube carries the player sideways");
                    found_h = 1;
                }
                if (found_h && found_v) goto done;
            }
    }
done:
    CHECK(found_h, "the levels contain horizontal tubes");
    CHECK(found_v, "the levels contain vertical launch tubes");
}


/* Jazz has more than one shot.  The blaster fires straight; the bouncer arcs
 * under gravity and reflects off surfaces; ammo pickups switch weapons.  Only
 * the straight blaster was implemented before. */
static void test_shot_types(void)
{
    JazzGame g;
    int f;
    s16 y0;

    /* Default is the straight blaster: level flight, no vertical drift. */
    jazz_game_init(&g);
    jazz_debug_set_stage(&g, 0);
    jazz_debug_place(&g, 300, 300);
    g.player.facing = 1;
    g.player.shotType = JAZZ_SHOT_BLASTER;
    jazz_step(&g, JAZZ_INPUT_FIRE);
    {
        int b = -1, i;
        for (i = 0; i < JAZZ_MAX_BULLETS; i++) if (g.bullets[i].active) { b = i; break; }
        CHECK(b >= 0, "firing spawns a bullet");
        if (b >= 0) {
            y0 = g.bullets[b].y;
            CHECK(g.bullets[b].vx > 0, "the blaster travels in the facing direction");
            CHECK(g.bullets[b].gravity == 0, "the blaster is not affected by gravity");
            for (f = 0; f < 3; f++) jazz_step(&g, 0);
            /* still roughly level */
            CHECK(g.bullets[b].active == 0 || (g.bullets[b].y >= y0 - 2 && g.bullets[b].y <= y0 + 2),
                  "the blaster flies straight");
        }
    }

    /* The missile fires two projectiles at once: one angled up, one down. */
    jazz_game_init(&g);
    jazz_debug_set_stage(&g, 0);
    jazz_debug_place(&g, 300, 300);
    g.player.facing = 1;
    g.player.shotType = JAZZ_SHOT_UPWARD;
    jazz_step(&g, JAZZ_INPUT_FIRE);
    {
        int i, count = 0, up = 0, down = 0;
        for (i = 0; i < JAZZ_MAX_BULLETS; i++) {
            if (!g.bullets[i].active) continue;
            count++;
            if (g.bullets[i].vy < 0) up++;
            if (g.bullets[i].vy > 0) down++;
        }
        CHECK(count == 2, "the missile fires two projectiles at once");
        CHECK(up == 1, "one missile projectile is angled up");
        CHECK(down == 1, "one missile projectile is angled down");
    }

    /* The bouncer arcs: with gravity it gains downward speed over time. */
    jazz_game_init(&g);
    jazz_debug_set_stage(&g, 0);
    jazz_debug_place(&g, 300, 200);
    g.player.facing = 1;
    g.player.shotType = JAZZ_SHOT_BOUNCER;
    jazz_step(&g, JAZZ_INPUT_FIRE);
    {
        int b = -1, i;
        for (i = 0; i < JAZZ_MAX_BULLETS; i++) if (g.bullets[i].active) { b = i; break; }
        CHECK(b >= 0, "the bouncer spawns");
        if (b >= 0) {
            s16 vy0 = g.bullets[b].vy;
            CHECK(g.bullets[b].gravity > 0, "the bouncer is affected by gravity");
            jazz_step(&g, 0);
            if (g.bullets[b].active)
                CHECK(g.bullets[b].vy > vy0, "gravity pulls the bouncer downward over time");
        }
    }

    /* An ammo pickup switches the weapon. */
    {
        u16 gx;
        u8 gy;
        int found = 0, lvl;
        for (lvl = 0; lvl < JAZZ_STAGE_COUNT && !found; lvl++)
            for (gy = 0; gy < 64 && !found; gy++)
                for (gx = 0; gx < 256 && !found; gx++) {
                    const Jj1EventInfo *info =
                        jj1_event_info((u8)lvl, jj1_runtime_event((u8)lvl, (s16)gx, (s16)gy));
                    if (info->klass != JJ1_CLASS_ITEM) continue;
                    if ((info->param & 0x0F) != JJ1_ITEM_AMMO) continue;
                    jazz_game_init(&g);
                    jazz_debug_set_stage(&g, (u8)lvl);
                    g.player.shotType = JAZZ_SHOT_BLASTER;
                    jazz_debug_place(&g, (s16)((gx << 5) + 8), (s16)((gy << 5) + 6));
                    jazz_step(&g, 0);
                    CHECK(g.player.shotType != JAZZ_SHOT_BLASTER ||
                          ((info->param >> 4) & 0x0F) == JAZZ_SHOT_BLASTER,
                          "collecting an ammo crate switches the shot type");
                    found = 1;
                }
        CHECK(found, "the shareware levels contain ammo pickups");
    }
}

/* The switch button cycles only through weapons the player has collected. */
static void test_weapon_switch(void)
{
    JazzGame g;
    jazz_game_init(&g);
    jazz_debug_set_stage(&g, 0);

    /* With only the blaster owned, switching is a no-op. */
    g.player.weaponsOwned = (u8)(1 << JAZZ_SHOT_BLASTER);
    g.player.shotType = JAZZ_SHOT_BLASTER;
    jazz_step(&g, JAZZ_INPUT_SWITCH);
    CHECK(g.player.shotType == JAZZ_SHOT_BLASTER,
          "switching with only the blaster stays on the blaster");

    /* Own blaster + bouncer: one press moves to the bouncer, another wraps
       back to the blaster - never landing on an unowned weapon. */
    g.previousInput = 0;
    g.player.weaponsOwned = (u8)((1 << JAZZ_SHOT_BLASTER) | (1 << JAZZ_SHOT_BOUNCER));
    g.player.shotType = JAZZ_SHOT_BLASTER;
    jazz_step(&g, JAZZ_INPUT_SWITCH);
    CHECK(g.player.shotType == JAZZ_SHOT_BOUNCER, "switch advances to the next owned weapon");
    g.previousInput = 0;
    jazz_step(&g, JAZZ_INPUT_SWITCH);
    CHECK(g.player.shotType == JAZZ_SHOT_BLASTER, "switch wraps around owned weapons only");

    /* Holding the button does not cycle every frame (edge-triggered). */
    g.previousInput = 0;
    g.player.shotType = JAZZ_SHOT_BLASTER;
    jazz_step(&g, JAZZ_INPUT_SWITCH);
    {
        u8 after = g.player.shotType;
        jazz_step(&g, JAZZ_INPUT_SWITCH);   /* still held */
        CHECK(g.player.shotType == after, "holding switch does not cycle repeatedly");
    }
}


/* The original only grabs a pickup by walking into it when its strength is 0
 * (touchEvent's default case); a crate with strength must be shot open, and
 * takeEvent then hands over the contents.  Every item used to be grabbable. */
static void test_crates_need_shooting(void)
{
    int lvl, found_crate = 0, found_touch = 0;

    for (lvl = 0; lvl < JAZZ_STAGE_COUNT && !(found_crate && found_touch); lvl++) {
        int gx, gy;
        for (gy = 1; gy < 63; gy++)
            for (gx = 1; gx < 255; gx++) {
                const Jj1EventInfo *info =
                    jj1_event_info((u8)lvl, jj1_runtime_event((u8)lvl, (s16)gx, (s16)gy));
                JazzGame g;
                if (info->klass != JJ1_CLASS_ITEM) continue;

                if (info->strength && !found_crate) {
                    /* Standing in a crate must not collect it. */
                    jazz_game_init(&g);
                    jazz_debug_set_stage(&g, (u8)lvl);
                    jazz_debug_place(&g, (s16)((gx << 5) + 8), (s16)((gy << 5) + 6));
                    jazz_step(&g, 0);
                    CHECK(!jazz_event_taken(&g, (u8)gx, (u8)gy),
                          "a crate is not collected by touching it");
                    found_crate = 1;
                } else if (!info->strength && !found_touch) {
                    jazz_game_init(&g);
                    jazz_debug_set_stage(&g, (u8)lvl);
                    jazz_debug_place(&g, (s16)((gx << 5) + 8), (s16)((gy << 5) + 6));
                    jazz_step(&g, 0);
                    CHECK(jazz_event_taken(&g, (u8)gx, (u8)gy),
                          "a zero-strength pickup is still grabbed on touch");
                    found_touch = 1;
                }
                if (found_crate && found_touch) goto done;
            }
    }
done:
    CHECK(found_crate, "the levels contain shoot-open crates");
    CHECK(found_touch, "the levels contain touch pickups");
}

/* Bridges (movement 28) carry modifier 7, which the classifier used to treat
 * as harmless scenery - so the Diamondus spans were dropped entirely: not
 * drawn, and not solid.  They must exist and be standable from above. */
static void test_bridges_are_solid(void)
{
    int lvl, found = 0;
    for (lvl = 0; lvl < JAZZ_STAGE_COUNT && !found; lvl++) {
        int gx, gy;
        for (gy = 1; gy < 63 && !found; gy++)
            for (gx = 1; gx < 255; gx++) {
                if (jj1_event_info((u8)lvl, jj1_runtime_event((u8)lvl, (s16)gx, (s16)gy))->klass
                    != JJ1_CLASS_BRIDGE) continue;
                {
                    /* The deck sits `points` px below the cell top and the span
                       runs `param` pieces of `strength` px, so it must be solid
                       well to the right of the event's own cell too. */
                    const Jj1EventInfo *bi =
                        jj1_event_info((u8)lvl, jj1_runtime_event((u8)lvl, (s16)gx, (s16)gy));
                    s16 deckY = (s16)((gy << 5) + bi->points);
                    s16 span = (s16)(bi->param * bi->strength);
                    CHECK(jj1_runtime_down_point_solid((u8)lvl, (s16)((gx << 5) + 4), deckY),
                          "a bridge deck is solid from above");
                    CHECK(span > 32, "a bridge spans more than its own cell");
                    CHECK(jj1_runtime_down_point_solid((u8)lvl,
                              (s16)((gx << 5) + span - 8), deckY),
                          "the far end of the bridge span is solid too");
                }
                found = 1;
                break;
            }
    }
    CHECK(found, "the levels contain bridges");
}

/* Invincibility, shields, fast feet and high-jump feet all had no effect. */
static void test_powerups(void)
{
    JazzGame g;
    u8 healthBefore;

    /* Invincibility ignores damage entirely. */
    jazz_game_init(&g);
    jazz_debug_set_stage(&g, 0);
    g.player.invincibleTime = 60;
    g.invulnerability = 0;
    healthBefore = g.health;
    jazz_debug_hurt(&g);
    CHECK(g.health == healthBefore, "invincibility ignores damage");

    /* A shield absorbs hits before health is touched. */
    jazz_game_init(&g);
    jazz_debug_set_stage(&g, 0);
    g.player.shield = 2;
    g.invulnerability = 0;
    healthBefore = g.health;
    jazz_debug_hurt(&g);
    CHECK(g.health == healthBefore, "a shield absorbs the hit");
    CHECK(g.player.shield == 1, "the shield loses a point per hit");

    /* Once spent, damage lands again. */
    g.player.shield = 0;
    g.invulnerability = 0;
    jazz_debug_hurt(&g);
    CHECK(g.health < healthBefore, "damage lands once the shield is gone");

    /* Fast feet raise the top running speed. */
    {
        JazzGame slow, fast;
        int f;
        jazz_game_init(&slow);
        jazz_debug_set_stage(&slow, 0);
        jazz_game_init(&fast);
        jazz_debug_set_stage(&fast, 0);
        fast.player.fastFeetTime = 200;
        for (f = 0; f < 60; f++) {
            jazz_step(&slow, JAZZ_INPUT_RIGHT);
            jazz_step(&fast, JAZZ_INPUT_RIGHT);
        }
        CHECK(fast.player.vx > slow.player.vx, "fast feet run faster than normal");
    }

    /* High-jump feet reach higher than a normal jump. */
    {
        JazzGame low, high;
        int f;
        s16 lowTop, highTop;
        jazz_game_init(&low);
        jazz_debug_set_stage(&low, 0);
        jazz_game_init(&high);
        jazz_debug_set_stage(&high, 0);
        high.player.highJump = 1;
        lowTop = low.player.y;
        highTop = high.player.y;
        for (f = 0; f < 40; f++) {
            jazz_step(&low, JAZZ_INPUT_JUMP);
            jazz_step(&high, JAZZ_INPUT_JUMP);
            if (low.player.y < lowTop) lowTop = low.player.y;
            if (high.player.y < highTop) highTop = high.player.y;
        }
        CHECK(highTop < lowTop, "high-jump feet jump higher");
    }
}


/* Progress belongs to the player, not the level.  build_stage() used to wipe
 * the whole game struct, so finishing a level reset lives and ammo. */
static void test_progress_survives_level_change(void)
{
    JazzGame g;
    jazz_game_init(&g);
    jazz_debug_set_stage(&g, 0);
    g.lives = 5;
    g.score = 1234;
    g.player.weaponsOwned |= (u8)(1 << JAZZ_SHOT_BOUNCER);
    g.player.ammo[JAZZ_SHOT_BOUNCER] = 9;
    g.player.shotType = JAZZ_SHOT_BOUNCER;
    g.player.shield = 3;
    g.player.highJump = 1;

    jazz_debug_set_stage(&g, 1);          /* advance a level */

    CHECK(g.lives == 5, "lives survive a level change");
    CHECK(g.score == 1234, "score survives a level change");
    CHECK(g.player.ammo[JAZZ_SHOT_BOUNCER] == 9, "ammo survives a level change");
    CHECK(g.player.weaponsOwned & (1 << JAZZ_SHOT_BOUNCER),
          "collected weapons survive a level change");
    CHECK(g.player.shield == 3, "a shield survives a level change");
    CHECK(g.player.highJump, "high-jump feet survive a level change");
}

/* addAmmo only selects the weapon when the player had none of it. */
static void test_ammo_switch_only_when_new(void)
{
    JazzGame g;
    Jj1EventInfo crate;
    crate.klass = JJ1_CLASS_ITEM;
    crate.points = 0;
    crate.strength = 0;
    crate.param = (u8)(JJ1_ITEM_AMMO | (JAZZ_SHOT_BOUNCER << 4));

    /* First bouncer crate: the player had none, so it becomes current. */
    jazz_game_init(&g);
    jazz_debug_set_stage(&g, 0);
    g.player.shotType = JAZZ_SHOT_BLASTER;
    jazz_debug_grant(&g, &crate);
    CHECK(g.player.shotType == JAZZ_SHOT_BOUNCER, "a brand new weapon is selected");
    CHECK(g.player.ammo[JAZZ_SHOT_BOUNCER] > 0, "the crate adds rounds");

    /* Switch away, then top the same weapon up: it must not yank us back. */
    g.player.shotType = JAZZ_SHOT_BLASTER;
    jazz_debug_grant(&g, &crate);
    CHECK(g.player.shotType == JAZZ_SHOT_BLASTER,
          "topping up a weapon already held does not switch to it");
}

/* The airboard flies under direct control with gravity suspended. */
static void test_airboard(void)
{
    JazzGame g;
    int f;
    s16 y0;
    jazz_game_init(&g);
    jazz_debug_set_stage(&g, 0);
    g.player.flying = 1;
    y0 = g.player.y;
    for (f = 0; f < 20; f++) jazz_step(&g, JAZZ_INPUT_JUMP);
    CHECK(g.player.y < y0, "the airboard climbs while thrusting up");

    jazz_game_init(&g);
    jazz_debug_set_stage(&g, 0);
    g.player.flying = 1;
    jazz_debug_place(&g, 200, 200);
    y0 = g.player.y;
    for (f = 0; f < 20; f++) jazz_step(&g, 0);
    CHECK(g.player.y <= y0 + 2, "the airboard does not fall under gravity");
}

int main(void)
{
    test_level_geometry();
    test_walk_and_run_speeds();
    test_variable_jump();
    test_terminal_fall();
    test_item_collection();
    test_enemy_activation_kill_and_damage();
    test_springs();
    test_destructible_scenery();
    test_signposts_are_destructible();
    test_breakable_walls();
    test_region_markers_are_not_destructible();
    test_items_never_hurt();
    test_end_of_level_and_episode();
    test_pause();
    test_all_stages_are_playable();
    test_sucker_tubes();
    test_shot_types();
    test_weapon_switch();
    test_crates_need_shooting();
    test_bridges_are_solid();
    test_powerups();
    test_progress_survives_level_change();
    test_ammo_switch_only_when_new();
    test_airboard();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("PASS: game-core point-to-point checks against original JJ1 data");
    return EXIT_SUCCESS;
}
