#ifndef JJ1_SFX_H
#define JJ1_SFX_H

#include "jazz_game.h"

/* XGM PCM sample IDs (user range 64+); channel 1 stays with the music. */
#define JJ1_SFX_FIRE   64
#define JJ1_SFX_JUMP   65
#define JJ1_SFX_PICKUP 66
#define JJ1_SFX_BOOM   67
#define JJ1_SFX_OW     68
#define JJ1_SFX_ONEUP  69
#define JJ1_SFX_YEAHOO 70
#define JJ1_SFX_FLAME  71
#define JJ1_SFX_PHOTON 72
#define JJ1_SFX_SPRING 73
#define JJ1_SFX_INVULN 74
#define JJ1_SFX_BIRDY  75
#define JJ1_SFX_ROCKET 76
#define JJ1_SFX_GODLIKE 77
#define JJ1_SFX_BLOCK3  78

/* JJ1 bullet definitions use slots 2, 16, 15 and 17 respectively.
 * The fifth bullet's slot is empty in the shareware levels: don't substitute
 * an unrelated voice or weapon effect. Zero is XGM's stop command, so the
 * caller must skip playback rather than play ID zero. */
static inline u8 jj1_shot_sfx(u8 shot)
{
    switch (shot) {
    case JAZZ_SHOT_BLASTER: return JJ1_SFX_FIRE;
    case JAZZ_SHOT_TOASTER: return JJ1_SFX_FLAME;
    case JAZZ_SHOT_UPWARD:  return JJ1_SFX_PHOTON;
    case JAZZ_SHOT_BOUNCER: return JJ1_SFX_ROCKET;
    default: return 0;
    }
}
#endif
