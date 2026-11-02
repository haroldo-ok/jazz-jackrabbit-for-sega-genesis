#ifndef JJ1_OPTIONS_H
#define JJ1_OPTIONS_H
#include <genesis.h>
extern u8 jj1_music_enabled, jj1_sfx_enabled, jj1_controls;
void jj1_music_start(const u8 *track);
void jj1_sfx_start(u8 id, u8 priority, SoundPCMChannel channel);
void jj1_options_load(void);
void jj1_options_save(void);
#endif
