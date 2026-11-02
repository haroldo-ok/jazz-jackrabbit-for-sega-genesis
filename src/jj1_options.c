#include <genesis.h>
#include "jj1_options.h"
u8 jj1_music_enabled = 1, jj1_sfx_enabled = 1, jj1_controls = 0;
void jj1_music_start(const u8 *track) { if (jj1_music_enabled) XGM_startPlay(track); }
void jj1_sfx_start(u8 id, u8 priority, SoundPCMChannel channel) {
    if (jj1_sfx_enabled) XGM_startPlayPCM(id, priority, channel);
}
