/* Host test: the bonus diamond opens the bonus level at the end of the stage. */
#include <stdio.h>
#include <stdlib.h>
#include "jazz_game.h"
#include "jj1_events.h"
#include "jj1_runtime.h"

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); exit(1); } } while (0)

static int find_diamond(u8 stage, int *gx, int *gy)
{
    int x, y;
    for (x = 0; x < 256; x++)
        for (y = 0; y < 64; y++) {
            const Jj1EventInfo *i = jj1_event_info(stage, jj1_runtime_event(stage, x, y));
            if (i->klass == JJ1_CLASS_ITEM && (i->param & 0x0F) == JJ1_ITEM_DIAMOND) { *gx = x; *gy = y; return 1; }
        }
    return 0;
}

int main(void)
{
    u8 stage, found = 0;
    for (stage = 0; stage < JAZZ_STAGE_COUNT; stage++) {
        JazzGame g; int gx, gy, i;
        jazz_game_init(&g);
        jazz_debug_set_stage(&g, stage);
        if (!find_diamond(stage, &gx, &gy)) {
            /* a stage without a diamond must never queue a bonus level */
            if ((u8)(stage + 1) < JAZZ_STAGE_COUNT) {
                g.transitionTimer = 1; jazz_step(&g, 0);
                CHECK(!g.bonusPending, "no diamond, no bonus");
            }
            continue;
        }
        found++;
        jazz_debug_place(&g, (s16)(gx * 32 + 8), (s16)(gy * 32 + 4));
        for (i = 0; i < 4 && !g.bonusGem; i++) jazz_step(&g, 0);
        CHECK(g.bonusGem, "touching the diamond sets the bonus flag");
        CHECK(!g.bonusPending, "bonus is not queued mid-level");
        g.transitionTimer = 1;
        jazz_step(&g, 0);
        CHECK(g.bonusPending, "finishing the level with the diamond queues the bonus level");
        jazz_step(&g, 0);
        CHECK(!g.bonusPending, "the bonus request lasts a single step");
        CHECK(!g.bonusGem, "the diamond is spent");
    }
    CHECK(found > 0, "at least one stage contains the bonus diamond");
    printf("PASS: bonus diamond opens the bonus level (%d stages with a diamond)\n", found);
    return 0;
}
