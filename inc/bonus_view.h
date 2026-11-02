#ifndef JJ1_BONUS_VIEW_H
#define JJ1_BONUS_VIEW_H
#include "bonus.h"
/* Plays bonus level `index` (0-2) to completion.  Returns BONUS_WON or
 * BONUS_LOST.  Takes over the display; the caller must rebuild its own. */
int bonus_play(u8 index, const u8 *music);
#endif
