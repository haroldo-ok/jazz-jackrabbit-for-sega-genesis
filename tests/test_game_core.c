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
    CHECK(find_event(0, JJ1_CLASS_ITEM, &gx, &gy), "an original item exists");
    before = g.stageGems;
    place_on_ground(&g, (s16)(gx << 5) + 8, (s16)(gy << 5));
    jazz_debug_place(&g, (s16)(gx << 5) + 8, (s16)(gy << 5) + 6);
    jazz_step(&g, 0);
    CHECK(g.stageGems == before + 1, "touching the original item cell collects it");
    CHECK(jazz_event_taken(&g, (u8)gx, (u8)gy), "collected item is marked taken");
    CHECK(g.score > 0, "collection scores points");
    jazz_step(&g, 0);
    CHECK(g.stageGems == before + 1, "an item is only collected once");
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
    CHECK(found == JAZZ_STAGE_COUNT, "every level has shootable destructible scenery");
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
    test_items_never_hurt();
    test_end_of_level_and_episode();
    test_pause();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("PASS: game-core point-to-point checks against original JJ1 data");
    return EXIT_SUCCESS;
}
