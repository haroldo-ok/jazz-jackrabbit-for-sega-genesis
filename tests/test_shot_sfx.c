#include <assert.h>
#include <stdio.h>
#define JAZZ_HOST
#include "jj1_sfx.h"
int main(void)
{
    assert(jj1_shot_sfx(JAZZ_SHOT_BLASTER) == JJ1_SFX_FIRE);
    assert(jj1_shot_sfx(JAZZ_SHOT_TOASTER) == JJ1_SFX_FLAME);
    assert(jj1_shot_sfx(JAZZ_SHOT_UPWARD) == JJ1_SFX_PHOTON);
    assert(jj1_shot_sfx(JAZZ_SHOT_BOUNCER) == JJ1_SFX_ROCKET);
    assert(jj1_shot_sfx(JAZZ_SHOT_SPECIAL) == 0);
    assert(jj1_shot_sfx(255) == 0);
    puts("PASS: all weapon sound IDs");
    return 0;
}
