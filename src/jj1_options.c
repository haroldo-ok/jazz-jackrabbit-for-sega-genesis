#include <genesis.h>
#include "jj1_options.h"
u8 jj1_music_enabled = 1, jj1_sfx_enabled = 1, jj1_controls = 0;
void jj1_music_start(const u8 *track) { if (jj1_music_enabled) XGM_startPlay(track); }
/* The music now plays drums on all four PCM channels with XGM priorities 0-3;
 * effects are raised above that so they always take their channel and the
 * music cannot cut them (the drum voice returns with its next hit). */
void jj1_sfx_start(u8 id, u8 priority, SoundPCMChannel channel) {
    if (jj1_sfx_enabled) XGM_startPlayPCM(id, (u8)(priority + 8), channel);
}
