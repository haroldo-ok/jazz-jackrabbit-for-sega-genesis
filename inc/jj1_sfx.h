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

/* Clips of the original SOUNDS.000 that only level data names (an item's own
 * pickup noise, a breaking block, ...).  Clips that already have an id above
 * are reused rather than registered twice. */
#define JJ1_SFX_EVT_MACHGUN 79
#define JJ1_SFX_EVT_FIRE1   80
#define JJ1_SFX_EVT_UPLOOP  81
#define JJ1_SFX_EVT_PHOTON  82
#define JJ1_SFX_EVT_WAIT    83
#define JJ1_SFX_EVT_ORB     84
#define JJ1_SFX_EVT_FLAMER  85
#define JJ1_SFX_EVT_ELECTR  86
#define JJ1_SFX_EVT_ROCKET2 87
#define JJ1_SFX_EVT_STOP    88
#define JJ1_SFX_EVT_BLOCK1  89

/* An event record's sound byte is the 1-based index into SOUNDS.000. */
static inline u8 jj1_event_sfx(u8 index)
{
    static const u8 table[23] = {
        0, JJ1_SFX_INVULN, JJ1_SFX_EVT_MACHGUN, JJ1_SFX_BOOM, JJ1_SFX_OW,
        JJ1_SFX_PICKUP, JJ1_SFX_EVT_FIRE1, JJ1_SFX_EVT_UPLOOP, JJ1_SFX_ONEUP,
        JJ1_SFX_EVT_PHOTON, JJ1_SFX_EVT_WAIT, JJ1_SFX_EVT_ORB, JJ1_SFX_JUMP,
        JJ1_SFX_GODLIKE, JJ1_SFX_YEAHOO, JJ1_SFX_BIRDY, JJ1_SFX_EVT_FLAMER,
        JJ1_SFX_EVT_ELECTR, JJ1_SFX_SPRING, JJ1_SFX_EVT_ROCKET2, JJ1_SFX_EVT_STOP,
        JJ1_SFX_BLOCK3, JJ1_SFX_EVT_BLOCK1
    };
    return (index < 23) ? table[index] : 0;
}

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
