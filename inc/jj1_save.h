#ifndef JJ1_SAVE_H
#define JJ1_SAVE_H
#include <genesis.h>
#include "jazz_game.h"
#define JJ1_SAVE_SLOTS 4
u8 jj1_save_valid(u8 slot);
u8 jj1_save_write(u8 slot, const JazzGame *game);
u8 jj1_save_read(u8 slot, JazzGame *game);
#endif
