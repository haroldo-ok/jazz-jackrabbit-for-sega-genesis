#ifndef JAZZ_GAME_H
#define JAZZ_GAME_H

/* Pure game simulation.  It deliberately has no VDP, sound, or controller
 * calls so the same code can be verified on the host and in a Genesis ROM. */
#ifdef JAZZ_HOST
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int8_t s8;
typedef int16_t s16;
#else
#include <types.h>
#endif

#define JAZZ_MAP_W 96
#define JAZZ_MAP_H 15
#define JAZZ_MAX_ENEMIES 10
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

enum {
    JAZZ_EVENT_NONE  = 0,
    JAZZ_EVENT_JUMP  = 1,
    JAZZ_EVENT_FIRE  = 2,
    JAZZ_EVENT_GEM   = 4,
    JAZZ_EVENT_HURT  = 8,
    JAZZ_EVENT_EXIT  = 16,
    JAZZ_EVENT_KILL  = 32
};

typedef struct {
    s16 x, y;
    s8 vx, vy;
    u8 facing;
    u8 onGround;
} JazzPlayer;

typedef struct {
    s16 x, y;
    s8 direction;
    u8 active;
    u8 patrolLeft;
    u8 patrolRight;
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

#endif
