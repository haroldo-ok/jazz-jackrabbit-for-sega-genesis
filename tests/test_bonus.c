/* Host tests for the bonus-level simulation against the converted real data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bonus.h"

static u8 *load(const char *dir, const char *name, size_t n)
{
    char path[512]; FILE *f; u8 *buf = malloc(n);
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f || fread(buf, 1, n, f) != n) { fprintf(stderr, "cannot read %s\n", path); exit(2); }
    fclose(f);
    return buf;
}
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (level %d)\n", msg, lv); return 1; } } while (0)

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "res/bonus";
    u8 *tiles = load(dir, "tiles.bin", 61440), *masks = load(dir, "masks.bin", 3840);
    int lv;
    for (lv = 0; lv < BONUS_LEVELS; lv++) {
        char g[32], e[32]; BonusGame b; int i, gemCells = 0, steps, r;
        u8 *grid, *events; u8 gemId = 0, clockId = 0, exitId = 0;
        snprintf(g, sizeof g, "grid%d.bin", lv); snprintf(e, sizeof e, "events%d.bin", lv);
        grid = load(dir, g, 65536); events = load(dir, e, 65536);
        bonus_init(&b, (u8)lv, tiles, masks, grid, events);
        for (i = 1; i < 16; i++) {
            if (b.evUsed[i] && b.evType[i] == 2) gemId = (u8)i;
            if (b.evUsed[i] && b.evType[i] == 1) clockId = (u8)i;
            if (b.evUsed[i] && b.evType[i] == 7) exitId = (u8)i;
        }
        CHECK(gemId && clockId && exitId, "level defines gem, clock and exit events");
        for (i = 0; i < 65536; i++) if (events[i] == gemId) gemCells++;
        CHECK(gemCells >= b.items, "enough gems on the map to win");
        CHECK(!bonus_blocked(&b, b.x, b.y), "start position is open floor");

        /* moving: hold UP for a while and the player must travel somewhere */
        { s32 sx = b.x, sy = b.y;
          for (i = 0; i < 40; i++) bonus_step(&b, BONUS_INPUT_UP);
          CHECK(b.x != sx || b.y != sy, "UP moves the player"); }
        /* turning changes direction */
        { s32 d = b.direction; for (i = 0; i < 20; i++) bonus_step(&b, BONUS_INPUT_RIGHT);
          CHECK(b.direction != d, "RIGHT turns the player"); }
        /* jumping leaves the ground and lands */
        bonus_init(&b, (u8)lv, tiles, masks, grid, events);
        bonus_step(&b, BONUS_INPUT_JUMP);
        CHECK(b.z > 0, "JUMP leaves the floor");
        for (i = 0; i < 200 && b.z > 0; i++) bonus_step(&b, 0);
        CHECK(b.z == 0, "player lands again");

        /* gem pickup: stand on a gem cell centre and step */
        for (i = 0; i < 65536 && events[i] != gemId; i++) ;
        bonus_init(&b, (u8)lv, tiles, masks, grid, events);
        b.x = ((s32)(i & 255) << 15) + 16384; b.y = ((s32)(i >> 8) << 15) + 16384;
        r = bonus_step(&b, 0);
        CHECK(b.gems == 1 && r == BONUS_CONTINUE, "gem is collected");
        CHECK(bonus_event(&b, i & 255, i >> 8) == 0, "collected gem disappears");
        b.x = ((s32)(i & 255) << 15) + 16384; b.y = ((s32)(i >> 8) << 15) + 16384;
        bonus_step(&b, 0);
        CHECK(b.gems == 1, "gem cannot be collected twice");

        /* clock adds a minute */
        for (i = 0; i < 65536 && events[i] != clockId; i++) ;
        if (i < 65536) {
            s32 before; bonus_init(&b, (u8)lv, tiles, masks, grid, events); before = b.endTime;
            b.x = ((s32)(i & 255) << 15) + 16384; b.y = ((s32)(i >> 8) << 15) + 16384;
            bonus_step(&b, 0);
            CHECK(b.endTime == before + 60000, "clock adds 60 seconds");
        }
        /* exit loses the level */
        for (i = 0; i < 65536 && events[i] != exitId; i++) ;
        bonus_init(&b, (u8)lv, tiles, masks, grid, events);
        b.x = ((s32)(i & 255) << 15) + 16384; b.y = ((s32)(i >> 8) << 15) + 16384;
        CHECK(bonus_step(&b, 0) == BONUS_LOST, "exit sign ends the level");
        /* collecting every needed gem wins */
        bonus_init(&b, (u8)lv, tiles, masks, grid, events);
        r = BONUS_CONTINUE;
        for (i = 0; i < 65536 && r == BONUS_CONTINUE; i++) if (events[i] == gemId) {
            b.x = ((s32)(i & 255) << 15) + 16384; b.y = ((s32)(i >> 8) << 15) + 16384;
            b.z = 0; r = bonus_step(&b, 0);
        }
        CHECK(r == BONUS_WON && b.gems == b.items, "collecting all required gems wins");
        /* timeout */
        bonus_init(&b, (u8)lv, tiles, masks, grid, events);
        for (steps = 0, r = 0; steps < 20000 && r == BONUS_CONTINUE; steps++) r = bonus_step(&b, 0);
        CHECK(r == BONUS_LOST && steps * BONUS_STEP_MS > b.endTime - 100, "time runs out");
        free(grid); free(events);
    }
    printf("PASS: bonus levels (3 levels: movement, jump, gems, clock, exit, win, timeout)\n");
    return 0;
}
