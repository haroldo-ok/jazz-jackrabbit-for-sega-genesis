/* Guards against the whole game silently degrading to the legacy prototype.
 *
 * The JJ1 runtime used to be selected only by -DJAZZ_JJ1_RUNTIME in the
 * project Makefile.  Any build that did not pass that flag (for instance one
 * driven by SGDK's stock makefile) still compiled and still drew the original
 * terrain, but ran the hand-made prototype stage instead: no event grid, so
 * no enemies, no items, no springs, and an 18-gem level.
 *
 * This file is deliberately compiled WITHOUT the flag.
 */
#include <stdio.h>
#include "jazz_game.h"

#ifndef JAZZ_JJ1_RUNTIME
#error "JAZZ_JJ1_RUNTIME must default on: a flagless build would ship the prototype level"
#endif

int main(void)
{
    JazzGame g;
    jazz_game_init(&g);

    /* The original level 0 event grid holds hundreds of collectables; the
       hand-made prototype stage holds exactly 18.  Assert the intent (this is
       the real grid) rather than an exact count, which legitimately moves as
       event IDs are reclassified. */
    if (g.stageGemTotal < 100) {
        printf("FAIL: flagless build has only %d items on stage 0; 18 means the "
               "prototype level is being compiled instead of the JJ1 grid\n",
               g.stageGemTotal);
        return 1;
    }
    printf("PASS: JJ1 runtime is the default (stage 0 has %d original items)\n",
           g.stageGemTotal);
    return 0;
}
