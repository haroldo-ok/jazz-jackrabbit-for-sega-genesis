#ifndef JJ1_BONUS_H
#define JJ1_BONUS_H

/* JJ1 bonus levels: pure simulation (no VDP/sound), shared by the ROM and the
 * host tests.  Positions use the original's fixed point: 10 fractional bits,
 * one 32x32 tile = 32768 units, a full turn = 1024 angle units. */
#ifdef JAZZ_HOST
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32;
#else
#include <types.h>
#endif

#define BONUS_LEVELS 3
#define BONUS_INPUT_UP    0x01
#define BONUS_INPUT_DOWN  0x02
#define BONUS_INPUT_LEFT  0x04
#define BONUS_INPUT_RIGHT 0x08
#define BONUS_INPUT_RUN   0x10   /* original "fire" */
#define BONUS_INPUT_JUMP  0x20

#define BONUS_CONTINUE 0
#define BONUS_WON      1
#define BONUS_LOST    -1

#define BONUS_STEP_MS 16
#define BONUS_MAX_REMOVED 128

typedef struct {
    /* level data (ROM) */
    const u8 *floorTiles;   /* 60 x 32x32 palette indices */
    const u8 *masks;        /* 60 x 64 collision cells */
    const u8 *grid;         /* 256x256 tile ids */
    const u8 *events;       /* 256x256 event ids */
    u8 evType[16], evPass[16], evUsed[16], evSound[16];
    u16 items;
    s32 endTime;            /* ms */
    s32 ticks;              /* ms */
    /* player */
    s32 x, y, z, dz, dr, da, direction;
    u8 running, crashed, jumping;
    u16 gems;
    /* collected events */
    u16 removed[BONUS_MAX_REMOVED];
    u8 removedCount;
    u8 removedRows[32];     /* bit per map row that has a removed cell */
    /* sound request for the last step: event sound id (SE index) or 0 */
    u8 sound;
    u8 gotTime;
} BonusGame;

s32 bonus_sin(s32 angle);
s32 bonus_cos(s32 angle);
void bonus_init(BonusGame *b, u8 level, const u8 *floorTiles, const u8 *masks,
                const u8 *grid, const u8 *events);
int bonus_step(BonusGame *b, u8 input);
/* event id at a tile, 0 if none or already collected */
u8 bonus_event(const BonusGame *b, int tx, int ty);
int bonus_blocked(const BonusGame *b, s32 x, s32 y);

#endif
