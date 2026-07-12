#include <stdio.h>
#include <stdlib.h>
#include "jazz_game.h"

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

static void test_stage_layout(void)
{
    JazzGame g;
    jazz_game_init(&g);
    CHECK(g.stage == 0, "stage 0 initializes");
    CHECK(g.lives == 3 && g.health == 5, "initial lives and health");
    CHECK(g.stageGemTotal == 18, "first stage has expected gem count");
    CHECK(jazz_is_solid(&g, 0, 13), "floor is solid");
    CHECK(jazz_tile_at(&g, 92, 11) == JAZZ_TILE_EXIT, "exit is present");
}

static void test_run_jump_and_pause(void)
{
    JazzGame g;
    s16 startX;
    s16 floorY;
    jazz_game_init(&g);
    startX = g.player.x;
    step_n(&g, JAZZ_INPUT_RIGHT, 20);
    CHECK(g.player.x > startX, "right input advances player");

    floorY = g.player.y;
    jazz_step(&g, JAZZ_INPUT_JUMP);
    jazz_step(&g, 0);
    step_n(&g, 0, 8);
    CHECK(g.player.y < floorY, "jump rises above floor");
    step_n(&g, 0, 60);
    CHECK(g.player.onGround && g.player.y == floorY, "jump lands on the floor");

    jazz_step(&g, JAZZ_INPUT_START);
    jazz_step(&g, 0);
    startX = g.player.x;
    step_n(&g, JAZZ_INPUT_RIGHT, 8);
    CHECK(g.player.x == startX, "pause freezes simulation");
    jazz_step(&g, JAZZ_INPUT_START);
    jazz_step(&g, 0);
    step_n(&g, JAZZ_INPUT_RIGHT, 8);
    CHECK(g.player.x > startX, "unpause resumes simulation");
}

static void test_combat_collection_and_damage(void)
{
    JazzGame g;
    jazz_game_init(&g);

    /* Point-to-point projectile collision: align Jazz and a turtle, then fire. */
    g.player.x = g.enemies[0].x - 20;
    g.player.y = g.enemies[0].y;
    g.player.facing = 1;
    jazz_step(&g, JAZZ_INPUT_FIRE);
    jazz_step(&g, 0);
    step_n(&g, 0, 12);
    CHECK(!g.enemies[0].active, "projectile removes enemy");
    CHECK(g.score >= 100, "enemy awards score");

    /* Point-to-point collectible collision. */
    g.player.x = g.gems[0].x;
    g.player.y = g.gems[0].y;
    jazz_step(&g, 0);
    CHECK(!g.gems[0].active && g.stageGems == 1, "contact collects gem");

    /* Damage occurs at most once while invulnerability is active. */
    g.enemies[1].x = g.player.x;
    g.enemies[1].y = g.player.y;
    jazz_step(&g, 0);
    CHECK(g.health == 4, "enemy contact damages player");
    step_n(&g, 0, 10);
    CHECK(g.health == 4, "invulnerability suppresses repeated contact damage");
}

static void test_complete_all_stages(void)
{
    JazzGame g;
    int i;
    jazz_game_init(&g);
    for (i = 0; i < JAZZ_STAGE_COUNT; i++) {
        g.player.x = (92 << 4) + 1;
        g.player.y = 11 << 4;
        jazz_step(&g, 0);
        CHECK(g.transitionTimer != 0, "exit starts stage transition");
        step_n(&g, 0, 80);
        if (i < (JAZZ_STAGE_COUNT - 1)) CHECK(g.stage == (u8)(i + 1), "exit loads next stage");
    }
    CHECK(g.finished, "third exit reaches completion state");
}

int main(void)
{
    test_stage_layout();
    test_run_jump_and_pause();
    test_combat_collection_and_damage();
    test_complete_all_stages();
    if (failures) {
        fprintf(stderr, "%d game-core test(s) failed.\n", failures);
        return EXIT_FAILURE;
    }
    printf("PASS: game-core point-to-point checks\n");
    return EXIT_SUCCESS;
}
