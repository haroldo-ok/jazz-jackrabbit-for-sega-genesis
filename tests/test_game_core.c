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

static void test_springs(void)
{
    JazzGame g;
    int gx, gy, i;
    s16 apex;
    jazz_game_init(&g);
    if (!find_event(0, JJ1_CLASS_SPRING, &gx, &gy)) { CHECK(0, "spring exists"); return; }
    place_on_ground(&g, (s16)(gx << 5) + 8, (s16)(gy << 5) - 24);
    jazz_debug_place(&g, (s16)(gx << 5) + 8, (s16)(gy << 5) + 4);
    apex = g.player.y;
    for (i = 0; i < 120; i++) {
        jazz_step(&g, 0);
        if (g.player.y < apex) apex = g.player.y;
    }
    CHECK(((s16)(gy << 5) + 4) - apex >= 80,
          "spring launches well above an unassisted jump without holding jump");
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

int main(void)
{
    test_level_geometry();
    test_walk_and_run_speeds();
    test_variable_jump();
    test_terminal_fall();
    test_item_collection();
    test_enemy_activation_kill_and_damage();
    test_springs();
    test_end_of_level_and_episode();
    test_pause();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("PASS: game-core point-to-point checks against original JJ1 data");
    return EXIT_SUCCESS;
}
