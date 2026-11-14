#include <genesis.h>
#include "jj1_options.h"
u8 jj1_music_enabled = 1, jj1_sfx_enabled = 1, jj1_controls = 0;
/* Lite cartridge: the tracks are in ROM banks 7-9, one of which is mapped at
 * $380000 (SSF2 register $A130FF).  A track's address carries its bank in the
 * top byte.  The Z80 is held off the bus while the bank changes and the new
 * track is started, so it never reads the wrong bank. */
void jj1_music_start(const u8 *track)
{
    const u32 v = (u32)track;
    u8 busTaken;
    if (!jj1_music_enabled) return;
    busTaken = Z80_getAndRequestBus(TRUE);
    *(volatile u8 *)0xA130FF = (u8)(v >> 24);
    XGM_startPlay((const u8 *)(v & 0xFFFFFF));
    if (!busTaken) Z80_releaseBus();
}
/* The music now plays drums on all four PCM channels with XGM priorities 0-3;
 * effects are raised above that so they always take their channel and the
 * music cannot cut them (the drum voice returns with its next hit). */
void jj1_sfx_start(u8 id, u8 priority, SoundPCMChannel channel) {
    if (jj1_sfx_enabled) XGM_startPlayPCM(id, (u8)(priority + 8), channel);
}
