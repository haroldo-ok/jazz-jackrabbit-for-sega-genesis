#ifndef JAZZ_GAME_H
#define JAZZ_GAME_H

/* Pure game simulation.  It deliberately has no VDP, sound, or controller
 * calls so the same code can be verified on the host and in a Genesis ROM. */

/* The JJ1 runtime (original masks, event grids, physics) is the real game;
 * the legacy hand-made prototype level survives only as a fallback.  Select
 * the JJ1 runtime HERE rather than relying on a -D flag in the project
 * Makefile: a build that uses SGDK's stock makefile, or any other build that
 * misses the flag, would otherwise silently produce the prototype ROM -
 * original terrain but no events, i.e. no enemies, items or springs and an
 * 18-gem hand-made stage.  Define JAZZ_LEGACY_PROTOTYPE to opt out. */
#if !defined(JAZZ_JJ1_RUNTIME) && !defined(JAZZ_LEGACY_PROTOTYPE)
#define JAZZ_JJ1_RUNTIME 1
#endif

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
#define JAZZ_MAX_ENEMIES 6   /* one 48-tile VRAM sprite slot each */
#define JAZZ_MAX_DESTRUCT_HITS 6
/* Collision body of an enemy, shared by the core and the renderer. */
#define JAZZ_ENEMY_W 14
#define JAZZ_ENEMY_H 16
#define JAZZ_MAX_GEMS 24
#define JAZZ_MAX_BULLETS 6   /* room for the missile's up+down pair per shot */
#define JAZZ_STAGE_COUNT 8   /* all eight shareware levels */

/* Powerup durations, in frames (the original counts milliseconds; these are
 * the equivalent at 60Hz, capped to fit a byte counter). */
#define JAZZ_INVINCIBLE_FRAMES 250
#define JAZZ_FASTFEET_FRAMES   250
/* Fast feet raise the run ceiling; high-jump feet raise the jump arc. */
#define JAZZ_PXS_FASTRUN    1800
#define JAZZ_HIGHJUMP_BONUS 16

/* Player animation states, mapped from the original PA_ indices.  Kept here
 * (not in jazz_gfx.h) so the game core can derive the state from physics. */
#define JJ1_PLAYER_STAND  0
#define JJ1_PLAYER_WALK   1
#define JJ1_PLAYER_RUN    2
#define JJ1_PLAYER_JUMP   3
#define JJ1_PLAYER_FALL   4
#define JJ1_PLAYER_SHOOT  5
#define JJ1_PLAYER_CROUCH 6
#define JJ1_PLAYER_LOOKUP 7
#define JJ1_PLAYER_SKID   8   /* PA_*STOP: skid/turn */
#define JJ1_PLAYER_HURT   9
#define JJ1_PLAYER_SPRING 10
#define JJ1_PLAYER_STATES 11

#define JAZZ_INPUT_LEFT  0x0001
#define JAZZ_INPUT_RIGHT 0x0002
#define JAZZ_INPUT_JUMP  0x0004
#define JAZZ_INPUT_FIRE  0x0008
#define JAZZ_INPUT_START 0x0010
#define JAZZ_INPUT_SWITCH 0x0020   /* cycle to the next available weapon */

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
    u8 shotType;       /* JAZZ_SHOT_*; blaster by default */
    u8 weaponsOwned;   /* bitmask of collected shot types (blaster always set) */
    u8 invincibleTime; /* frames of powerup invincibility left (modifier 1) */
    u8 fastFeetTime;   /* frames of fast feet left (modifier 26) */
    u8 shield;         /* shield hits remaining (modifiers 33/36) */
    u8 highJump;       /* high-jump feet collected (modifier 5) */
    u8 inTube;         /* inside a sucker tube this frame: tube drives motion */
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
    s16 vx, vy;        /* 8.8 px/frame, so shots can rise and fall */
    s16 subX, subY;    /* 8.8 subpixel accumulators */
    s8 gravity;        /* 8.8 accel per frame; bouncer/spread arc */
    u8 behaviour;      /* 0 straight, 4 bouncer (reflects off surfaces) */
    u8 active;
} JazzBullet;

/* Player shot types, mapped from the level's bullet definitions (bulletSet):
 * the default blaster plus four weapons granted by ammo crates. */
#define JAZZ_SHOT_BLASTER 0   /* straight, slow */
#define JAZZ_SHOT_TOASTER 1   /* straight, fast */
#define JAZZ_SHOT_UPWARD  2   /* fast, angled up */
#define JAZZ_SHOT_BOUNCER 3   /* arcs under gravity, reflects off surfaces */
#define JAZZ_SHOT_SPECIAL 4   /* fourth weapon crate */
#define JAZZ_SHOT_TYPES   5

typedef struct {
    s16 x, y;
    u8 active;
} JazzGem;

/* Damage in progress on a destructible cell (see JJ1 behaviour 21). */
typedef struct {
    u8 gridX, gridY;
    u8 hits;
    u8 active;
} JazzDestructHit;

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
    /* One bit per cell: destructible block already shot away.  Kept separate
       from `taken` because it also has to make the terrain non-solid. */
    u8 destroyed[(256 * 64) / 8];
    /* Partial damage for destructibles needing more than one shot. */
    JazzDestructHit destructHits[JAZZ_MAX_DESTRUCT_HITS];
    /* Bumped whenever a block is destroyed, so the renderer can redraw it. */
    u16 destroyCount;
    u8 destroyedX, destroyedY;
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
/* True once a destructible block at this cell has been shot away. */
u8 jazz_cell_destroyed(const JazzGame *game, u8 gridX, u8 gridY);
void jazz_debug_place(JazzGame *game, s16 x, s16 y);
/* Rebuild the game on a given stage (tests only). */
void jazz_debug_set_stage(JazzGame *game, u8 stage);
void jazz_debug_hurt(JazzGame *game);
/* Y coordinate of the player's feet, i.e. the bottom of the collision body. */
s16 jazz_player_feet(const JazzGame *game);
/* Current player animation state (JJ1_PLAYER_* in jazz_gfx.h), derived from
 * the same physics the ROM and tests share. */
u8 jazz_player_anim_state(const JazzGame *game, u16 input);
#endif

#endif
