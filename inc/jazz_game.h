#ifndef JAZZ_GAME_H
#define JAZZ_GAME_H

/* Pure game simulation.  It deliberately has no VDP, sound, or controller
 * calls so the same code can be verified on the host and in a Genesis ROM. */
#ifdef JAZZ_HOST
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
#else
#include <types.h>
#endif

#define JAZZ_MAP_W 96
#define JAZZ_MAP_H 15
#define JAZZ_MAX_ENEMIES 8
#define JAZZ_MAX_GEMS 24
#define JAZZ_MAX_BULLETS 4
#define JAZZ_STAGE_COUNT 3

#define JAZZ_INPUT_LEFT  0x0001
#define JAZZ_INPUT_RIGHT 0x0002
#define JAZZ_INPUT_JUMP  0x0004
#define JAZZ_INPUT_FIRE  0x0008
#define JAZZ_INPUT_START 0x0010

#define JAZZ_TILE_EMPTY    0
#define JAZZ_TILE_SOLID    1
#define JAZZ_TILE_PLATFORM 2
#define JAZZ_TILE_EXIT     3

/* Player velocities/accelerations are 8.8 fixed-point pixels per frame,
 * converted from the original game's movement model (as reimplemented by
 * the GPL OpenJazz engine): walk 2.50 px/f, run 5.08 px/f, gravity applied
 * only while rising, an immediate terminal fall speed, and a target-height
 * jump whose ascent velocity is recomputed each frame and cancelled when
 * the button is released. */
#define JAZZ_FIX(px)        ((s16)((px) << 8))
#define JAZZ_PXS_WALK       640    /* 2.50 px/frame */
#define JAZZ_PXS_RUN        1300   /* 5.08 px/frame */
#define JAZZ_PYS_FALL       1400   /* 5.47 px/frame terminal fall */
#define JAZZ_PYS_JUMP       (-1400)
#define JAZZ_PXA_REVERSE    112    /* 0.44 px/frame^2 */
#define JAZZ_PXA_STOP       64     /* 0.25 px/frame^2 */
#define JAZZ_PXA_WALK       64
#define JAZZ_PXA_RUN        64
#define JAZZ_PYA_GRAVITY    144    /* 0.56 px/frame^2 while rising */
#define JAZZ_PYO_JUMP       84     /* base jump height, pixels */
#define JAZZ_NO_JUMP_TARGET 0x7FFF

enum {
    JAZZ_EVENT_NONE  = 0,
    JAZZ_EVENT_JUMP  = 1,
    JAZZ_EVENT_FIRE  = 2,
    JAZZ_EVENT_GEM   = 4,
    JAZZ_EVENT_HURT  = 8,
    JAZZ_EVENT_EXIT  = 16,
    JAZZ_EVENT_KILL  = 32,
    JAZZ_EVENT_HEALTH = 64,
    JAZZ_EVENT_LIFE   = 128
};

typedef struct {
    s16 x, y;          /* pixels, top-left of the 14x22 body */
    s16 subX, subY;    /* signed 8.8 sub-pixel remainders */
    s16 vx, vy;        /* 8.8 px/frame */
    s16 jumpTargetY;   /* pixels; JAZZ_NO_JUMP_TARGET when not ascending */
    u8 facing;
    u8 onGround;
    u8 springJump;     /* current ascent came from a spring: ignore button */
} JazzPlayer;

typedef struct {
    s16 x, y;          /* pixels */
    s16 homeX, homeY;  /* spawn anchor, pixels */
    s8 direction;
    u8 active;
    u8 klass;          /* JJ1_CLASS_ENEMY_WALK / _FLY / JJ1_CLASS_HAZARD */
    u8 gridX, gridY;   /* originating event cell */
    u8 hitPoints;
    u8 phase;          /* flyer bob phase */
} JazzEnemy;

typedef struct {
    s16 x, y;
    s8 vx;
    u8 active;
} JazzBullet;

typedef struct {
    s16 x, y;
    u8 active;
} JazzGem;

typedef struct {
    /* Volatile is intentional: a 68000 traps on odd word accesses.  It prevents
     * modern GCC from folding adjacent byte-cell stores into an unaligned word. */
    volatile u8 tiles[JAZZ_MAP_H][JAZZ_MAP_W];
    JazzPlayer player;
    JazzEnemy enemies[JAZZ_MAX_ENEMIES];
    JazzBullet bullets[JAZZ_MAX_BULLETS];
    JazzGem gems[JAZZ_MAX_GEMS];
#ifdef JAZZ_JJ1_RUNTIME
    /* One bit per 32x32 event cell: item collected / enemy killed. */
    u8 taken[(256 * 64) / 8];
    u16 enemyScanColumn;
#endif
    u16 previousInput;
    u16 frame;
    u16 score;
    u8 stage;
    u8 lives;
    u8 health;
    u8 stageGems;
    u8 stageGemTotal;
    u8 fireCooldown;
    u8 invulnerability;
    u8 paused;
    u8 transitionTimer;
    u8 finished;
    u8 events;
} JazzGame;

void jazz_game_init(JazzGame *game);
void jazz_step(JazzGame *game, u16 input);
u8 jazz_is_solid(const JazzGame *game, s16 tx, s16 ty);
u8 jazz_tile_at(const JazzGame *game, s16 tx, s16 ty);
#ifdef JAZZ_JJ1_RUNTIME
u8 jazz_event_taken(const JazzGame *game, u8 gridX, u8 gridY);
void jazz_debug_place(JazzGame *game, s16 x, s16 y);
#endif

#endif
