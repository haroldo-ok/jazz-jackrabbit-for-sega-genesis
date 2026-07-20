#include "jazz_game.h"

#define PLAYER_W 14
#define PLAYER_H 22
#define ENEMY_W JAZZ_ENEMY_W
#define ENEMY_H JAZZ_ENEMY_H

#ifdef JAZZ_JJ1_RUNTIME
#define WORLD_W (256 * 32)
#define WORLD_H (64 * 32)
#include "jj1_events.h"
#include "jj1_runtime.h"
#else
#define WORLD_W (JAZZ_MAP_W * 16)
#define WORLD_H (JAZZ_MAP_H * 16)
#endif

static void clear_game(JazzGame *g)
{
    u16 i;
    u8 x, y;
    /* Keep tilemap writes byte-wide: neighboring cells can be at odd addresses. */
    for (y = 0; y < JAZZ_MAP_H; y++)
        for (x = 0; x < JAZZ_MAP_W; x++) g->tiles[y][x] = JAZZ_TILE_EMPTY;
    for (i = 0; i < JAZZ_MAX_ENEMIES; i++) g->enemies[i].active = 0;
    for (i = 0; i < JAZZ_MAX_GEMS; i++) g->gems[i].active = 0;
    for (i = 0; i < JAZZ_MAX_BULLETS; i++) g->bullets[i].active = 0;
#ifdef JAZZ_JJ1_RUNTIME
    for (i = 0; i < sizeof(g->taken); i++) g->taken[i] = 0;
    for (i = 0; i < sizeof(g->destroyed); i++) g->destroyed[i] = 0;
    for (i = 0; i < JAZZ_MAX_DESTRUCT_HITS; i++) g->destructHits[i].active = 0;
    g->destroyCount = 0;
    g->destroyedX = 0;
    g->destroyedY = 0;
    g->enemyScanColumn = 0;
#endif
}

#ifndef JAZZ_JJ1_RUNTIME
static void set_tile(JazzGame *g, s16 x, s16 y, u8 value)
{
    if ((x >= 0) && (x < JAZZ_MAP_W) && (y >= 0) && (y < JAZZ_MAP_H)) g->tiles[y][x] = value;
}
#endif

u8 jazz_tile_at(const JazzGame *g, s16 tx, s16 ty)
{
    if ((tx < 0) || (tx >= JAZZ_MAP_W) || (ty < 0) || (ty >= JAZZ_MAP_H)) return JAZZ_TILE_SOLID;
    return g->tiles[ty][tx];
}

#ifdef JAZZ_JJ1_RUNTIME
/* Destructible scenery (JJ1 behaviour 21) is ordinary solid terrain until it
 * has taken `strength` shots, at which point the original engine swaps the
 * block out (setTile to multiA) and the space becomes passable.  We keep a
 * one-bit-per-cell overlay instead of rewriting the ROM block map, and every
 * collision probe below consults it, so a destroyed block stops blocking both
 * the player and his bullets. */
u8 jazz_cell_destroyed(const JazzGame *g, u8 gridX, u8 gridY)
{
    u16 bit = ((u16)gridY << 8) + gridX;
    return (u8)((g->destroyed[bit >> 3] >> (bit & 7)) & 1);
}

static u8 point_destroyed(const JazzGame *g, s16 x, s16 y)
{
    s16 gx = x >> 5, gy = y >> 5;
    if ((gx < 0) || (gx > 255) || (gy < 0) || (gy > 63)) return 0;
    return jazz_cell_destroyed(g, (u8)gx, (u8)gy);
}

static u8 pt_solid(const JazzGame *g, s16 x, s16 y)
{
    if (!jj1_runtime_point_solid(g->stage, x, y)) return 0;
    return !point_destroyed(g, x, y);
}

static u8 down_pt_solid(const JazzGame *g, s16 x, s16 y)
{
    if (!jj1_runtime_down_point_solid(g->stage, x, y)) return 0;
    return !point_destroyed(g, x, y);
}
#endif

u8 jazz_is_solid(const JazzGame *g, s16 tx, s16 ty)
{
#ifdef JAZZ_JJ1_RUNTIME
    return pt_solid(g, (s16)(tx << 4), (s16)(ty << 4)) ||
           pt_solid(g, (s16)((tx << 4) + 15), (s16)((ty << 4) + 15));
#else
    u8 tile = jazz_tile_at(g, tx, ty);
    return (tile == JAZZ_TILE_SOLID) || (tile == JAZZ_TILE_PLATFORM);
#endif
}

static u8 rect_hits_solid(const JazzGame *g, s16 x, s16 y, s16 w, s16 h)
{
    if ((x < 0) || (y < 0) || ((x + w - 1) >= WORLD_W) || ((y + h - 1) >= WORLD_H)) return 1;
#ifdef JAZZ_JJ1_RUNTIME
    {
        /* Sample the 4px mask grid so a block that has been shot away does not
           keep blocking, which jj1_runtime_rect_solid alone cannot know. */
        s16 px, py;
        for (py = y; py < y + h; py += 4)
            for (px = x; px < x + w; px += 4)
                if (pt_solid(g, px, py)) return 1;
        for (py = y; py < y + h; py += 4)
            if (pt_solid(g, (s16)(x + w - 1), py)) return 1;
        for (px = x; px < x + w; px += 4)
            if (pt_solid(g, px, (s16)(y + h - 1))) return 1;
        return pt_solid(g, (s16)(x + w - 1), (s16)(y + h - 1));
    }
#else
    s16 tx0, tx1, ty0, ty1, tx, ty;
    tx0 = x >> 4; tx1 = (x + w - 1) >> 4;
    ty0 = y >> 4; ty1 = (y + h - 1) >> 4;
    for (ty = ty0; ty <= ty1; ty++)
        for (tx = tx0; tx <= tx1; tx++)
            if (jazz_is_solid(g, tx, ty)) return 1;
    return 0;
#endif
}

static u8 rect_overlap(s16 ax, s16 ay, s16 aw, s16 ah, s16 bx, s16 by, s16 bw, s16 bh)
{
    return (ax < (bx + bw)) && ((ax + aw) > bx) && (ay < (by + bh)) && ((ay + ah) > by);
}

#ifdef JAZZ_JJ1_RUNTIME

u8 jazz_event_taken(const JazzGame *g, u8 gridX, u8 gridY)
{
    u16 bit = ((u16)gridY << 8) + gridX;
    return (g->taken[bit >> 3] >> (bit & 7)) & 1;
}

static void mark_event_taken(JazzGame *g, u8 gridX, u8 gridY)
{
    u16 bit = ((u16)gridY << 8) + gridX;
    g->taken[bit >> 3] |= (u8)(1 << (bit & 7));
}

static void count_stage_items(JazzGame *g)
{
    u16 gx;
    u8 gy;
    g->stageGemTotal = 0;
    for (gy = 0; gy < 64; gy++)
        for (gx = 0; gx < 256; gx++)
            if (jj1_event_info(g->stage, jj1_runtime_event(g->stage, gx, gy))->klass
                == JJ1_CLASS_ITEM) g->stageGemTotal++;
}

#else /* !JAZZ_JJ1_RUNTIME */

static void add_platform(JazzGame *g, u8 x, u8 y, u8 length)
{
    u8 i;
    for (i = 0; i < length; i++) set_tile(g, x + i, y, JAZZ_TILE_PLATFORM);
}

static void spawn_gem(JazzGame *g, u8 slot, u8 tx, u8 ty)
{
    if (slot >= JAZZ_MAX_GEMS) return;
    g->gems[slot].active = 1;
    g->gems[slot].x = ((s16)tx << 4) + 4;
    g->gems[slot].y = ((s16)ty << 4) + 2;
    g->stageGemTotal++;
}

static void spawn_enemy(JazzGame *g, u8 slot, u8 tx, u8 left, u8 right)
{
    JazzEnemy *e;
    if (slot >= JAZZ_MAX_ENEMIES) return;
    e = &g->enemies[slot];
    e->active = 1;
    e->x = ((s16)tx << 4) + 1;
    e->y = (13 << 4) - ENEMY_H;
    e->homeX = ((s16)left << 4);
    e->homeY = ((s16)right << 4); /* legacy path reuses home fields as patrol bounds */
    e->direction = 1;
    e->klass = 0;
    e->hitPoints = 1;
}

#endif

static void build_stage(JazzGame *g, u8 stage)
{
    /* Progress belongs to the player, not the level: the original reloads the
       level around a Player that survives, so lives, score, weapons and
       powerups carry over.  clear_game() wipes the whole struct, which is why
       finishing a level used to reset ammo and lives. */
    u8 carryLives = g->lives;
    u32 carryScore = g->score;
    u8 carryShot = g->player.shotType;
    u8 carryOwned = g->player.weaponsOwned;
    u8 carryShield = g->player.shield;
    u8 carryHighJump = g->player.highJump;
    u8 carryAmmo[JAZZ_SHOT_TYPES];
    u8 ai;
    for (ai = 0; ai < JAZZ_SHOT_TYPES; ai++) carryAmmo[ai] = g->player.ammo[ai];

    clear_game(g);
    g->stage = stage;
    g->health = 5;
    g->fireCooldown = 0;
    g->invulnerability = 0;
    g->paused = 0;
    g->transitionTimer = 0;
    g->stageGems = 0;
    g->stageGemTotal = 0;
    g->player.x = 32;
    g->player.y = (13 << 4) - PLAYER_H;
    g->player.subX = 0;
    g->player.subY = 0;
    g->player.vx = 0;
    g->player.vy = 0;
    g->player.jumpTargetY = JAZZ_NO_JUMP_TARGET;
    g->player.springJump = 0;
    g->lives = carryLives;
    g->score = carryScore;
    g->player.shotType = carryShot;
    g->player.weaponsOwned = (u8)(carryOwned | (1 << JAZZ_SHOT_BLASTER));
    for (ai = 0; ai < JAZZ_SHOT_TYPES; ai++) g->player.ammo[ai] = carryAmmo[ai];
    g->player.shield = carryShield;
    g->player.highJump = carryHighJump;
    /* Timed effects and the airboard do not survive the level. */
    g->player.invincibleTime = 0;
    g->player.fastFeetTime = 0;
    g->player.flying = 0;
    g->player.inTube = 0;
    g->player.facing = 1;
    g->player.onGround = 1;

#ifdef JAZZ_JJ1_RUNTIME
    jj1_runtime_place_player(g, stage);
    count_stage_items(g);
#else
    {
        u8 x, i;
        for (x = 0; x < JAZZ_MAP_W; x++) {
            set_tile(g, x, 13, JAZZ_TILE_SOLID);
            set_tile(g, x, 14, JAZZ_TILE_SOLID);
        }
        if (stage == 0) {
            add_platform(g, 10, 10, 5); add_platform(g, 23, 9, 6);
            add_platform(g, 38, 11, 5); add_platform(g, 52, 8, 6);
            for (i = 0; i < 18; i++) spawn_gem(g, i, 5 + (i * 5), (i & 1) ? 11 : 12);
            spawn_enemy(g, 0, 18, 16, 22); spawn_enemy(g, 1, 34, 31, 38);
        } else if (stage == 1) {
            add_platform(g, 8, 11, 6); add_platform(g, 19, 8, 5);
            for (i = 0; i < 20; i++) spawn_gem(g, i, 4 + (i * 4), (i % 3) ? 12 : 10);
            spawn_enemy(g, 0, 15, 12, 18); spawn_enemy(g, 1, 28, 25, 32);
        } else {
            add_platform(g, 11, 9, 5); add_platform(g, 25, 11, 6);
            for (i = 0; i < 22; i++) spawn_gem(g, i, 4 + (i * 4), (i & 1) ? 12 : 11);
            spawn_enemy(g, 0, 17, 14, 21); spawn_enemy(g, 1, 36, 32, 39);
        }
        set_tile(g, 92, 10, JAZZ_TILE_EXIT);
        set_tile(g, 92, 11, JAZZ_TILE_EXIT);
        set_tile(g, 92, 12, JAZZ_TILE_EXIT);
    }
#endif
}

void jazz_game_init(JazzGame *g)
{
    u16 i;
    /* clear the whole public state without relying on a hosted C library */
    for (i = 0; i < (sizeof(JazzGame) / 2); i++) ((u16 *)g)[i] = 0;
    g->lives = 3;
    build_stage(g, 0);
}

static void respawn_or_lose_life(JazzGame *g)
{
    if (g->lives) g->lives--;
    if (!g->lives) {
        g->finished = 1;
        return;
    }
    g->health = 5;
    g->player.x = 32;
    g->player.y = (13 << 4) - PLAYER_H;
    g->player.vx = 0;
    g->player.vy = 0;
    g->player.jumpTargetY = JAZZ_NO_JUMP_TARGET;
    g->player.springJump = 0;
    g->player.onGround = 1;
#ifdef JAZZ_JJ1_RUNTIME
    jj1_runtime_place_player(g, g->stage);
#endif
    g->invulnerability = 90;
}

static void hurt_player(JazzGame *g)
{
    if (g->invulnerability) return;
    /* Powerup invincibility ignores damage outright. */
    if (g->player.invincibleTime) return;
    /* A shield absorbs one hit per point of capacity before health is lost. */
    if (g->player.shield) {
        g->player.shield--;
        g->invulnerability = 55;
        g->events |= JAZZ_EVENT_HURT;
        return;
    }
    g->events |= JAZZ_EVENT_HURT;
    g->invulnerability = 55;
    if (g->health) g->health--;
    if (!g->health) respawn_or_lose_life(g);
}

static u8 player_side_blocked(const JazzGame *g, s16 x, s16 y, s8 step)
{
#ifdef JAZZ_JJ1_RUNTIME
    s16 probeX = x + ((step > 0) ? (PLAYER_W - 1) : 0);
    return pt_solid(g, probeX, y + 4) ||
           pt_solid(g, probeX, y + (PLAYER_H >> 1)) ||
           pt_solid(g, probeX, y + PLAYER_H - 3);
#else
    (void) step;
    return rect_hits_solid(g, x, y, PLAYER_W, PLAYER_H);
#endif
}

static u8 player_vertical_blocked(const JazzGame *g, s16 x, s16 y, s8 step)
{
#ifdef JAZZ_JJ1_RUNTIME
    s16 probeY = y + ((step > 0) ? (PLAYER_H - 1) : 0);
    if (step > 0)
        return down_pt_solid(g, x + 2, probeY) ||
               down_pt_solid(g, x + (PLAYER_W >> 1), probeY) ||
               down_pt_solid(g, x + PLAYER_W - 3, probeY);
    return pt_solid(g, x + 2, probeY) ||
           pt_solid(g, x + (PLAYER_W >> 1), probeY) ||
           pt_solid(g, x + PLAYER_W - 3, probeY);
#else
    (void) step;
    return rect_hits_solid(g, x, y, PLAYER_W, PLAYER_H);
#endif
}

#ifdef JAZZ_JJ1_RUNTIME
/* Distance in pixels from Jazz's feet to the next solid 4px JJ1 mask cell.
 * A larger forward distance means a descending slope, never a step-up. */
static s8 jj1_ground_distance(const JazzGame *g, s16 x, s16 y)
{
    s8 distance;
    for (distance = 0; distance <= 8; distance++) {
        if (down_pt_solid(g, x + (PLAYER_W >> 1), y + PLAYER_H + distance))
            return distance;
    }
    return 9;
}
#endif

static void move_player_x(JazzGame *g, s8 amount)
{
    s8 step = (amount < 0) ? -1 : 1;
    while (amount) {
        if (player_side_blocked(g, g->player.x + step, g->player.y, step)) {
#ifdef JAZZ_JJ1_RUNTIME
            if (g->player.onGround) {
                s8 currentGround = jj1_ground_distance(g, g->player.x, g->player.y);
                s8 forwardGround = jj1_ground_distance(g, g->player.x + step, g->player.y);

                /* Descending ramp: follow its mask down. Never apply lift. */
                if ((forwardGround > currentGround) && (forwardGround <= 8)) {
                    s8 drop = forwardGround - currentGround;
                    if (!player_side_blocked(g, g->player.x + step, g->player.y + drop, step)) {
                        g->player.x += step;
                        g->player.y += drop;
                        amount -= step;
                        continue;
                    }
                }

                /* Rising ramp: lift at most one 4px JJ1 mask cell. */
                if (forwardGround <= currentGround) {
                    s8 lift;
                    for (lift = 1; lift <= 4; lift++) {
                        if (!player_side_blocked(g, g->player.x + step, g->player.y - lift, step) &&
                            !player_vertical_blocked(g, g->player.x + step, g->player.y - lift, -1)) {
                            g->player.y -= lift;
                            g->player.x += step;
                            amount -= step;
                            break;
                        }
                    }
                    if (lift <= 4) continue;
                }
            }
#endif
            g->player.vx = 0;
            return;
        }
        g->player.x += step;
        amount -= step;
    }
}

static void move_player_y(JazzGame *g, s8 amount)
{
    s8 step = (amount < 0) ? -1 : 1;
    g->player.onGround = 0;
    while (amount) {
        if (player_vertical_blocked(g, g->player.x, g->player.y + step, step)) {
            if (step > 0) g->player.onGround = 1;
            else g->player.jumpTargetY = JAZZ_NO_JUMP_TARGET; /* bonked a ceiling */
            g->player.vy = 0;
            return;
        }
        g->player.y += step;
        amount -= step;
    }
}

/* Per-type shot parameters, derived from the level's bullet table (xspeed,
 * yspeed, gravity, behaviour).  Speeds are 8.8 px/frame; the bouncer arcs and
 * reflects off surfaces (behaviour 4). */
/* A weapon can fire a second projectile at once (the missile fires one angled
 * up and one angled down).  shot_params fills the primary velocity, and when
 * *vy2 differs from *vy the caller spawns a second bullet with (*vx, *vy2).
 * JAZZ_NO_SECOND means the weapon fires a single projectile. */
#define JAZZ_NO_SECOND 0x7FFF

static void shot_params(u8 type, s16 *vx, s16 *vy, s16 *vy2,
                        s8 *gravity, u8 *behaviour)
{
    *vy2 = JAZZ_NO_SECOND;
    switch (type) {
    case JAZZ_SHOT_TOASTER: *vx = 1400; *vy = 0;    *gravity = 0;  *behaviour = 0; break;
    case JAZZ_SHOT_UPWARD:  /* missile: fires up AND down at once */
                            *vx = 1400; *vy = -256; *vy2 = 256; *gravity = 0; *behaviour = 0; break;
    case JAZZ_SHOT_BOUNCER: *vx = 700;  *vy = -768; *gravity = 24; *behaviour = 4; break;
    case JAZZ_SHOT_SPECIAL: *vx = 1600; *vy = 0;    *gravity = 0;  *behaviour = 0; break;
    case JAZZ_SHOT_BLASTER:
    default:                *vx = 1000; *vy = 0;    *gravity = 0;  *behaviour = 0; break;
    }
}

static s8 consume_subpixels(s16 velocity, s16 *acc);

/* Spawn one projectile into a free slot; returns 0 if none free. */
static u8 spawn_bullet(JazzGame *g, s16 vx, s16 vy, s8 gravity, u8 behaviour)
{
    u8 i;
    for (i = 0; i < JAZZ_MAX_BULLETS; i++) {
        JazzBullet *b = &g->bullets[i];
        if (b->active) continue;
        b->active = 1;
        b->x = g->player.x + (g->player.facing ? PLAYER_W : -2);
        b->y = g->player.y + 9;
        b->vx = g->player.facing ? vx : (s16)-vx;
        b->vy = vy;
        b->subX = 0;
        b->subY = 0;
        b->gravity = gravity;
        b->behaviour = behaviour;
        return 1;
    }
    return 0;
}

static void fire_bullet(JazzGame *g)
{
    s16 vx, vy, vy2;
    s8 gravity;
    u8 behaviour;
    shot_params(g->player.shotType, &vx, &vy, &vy2, &gravity, &behaviour);
    if (!spawn_bullet(g, vx, vy, gravity, behaviour)) return;
    /* Spread weapons (the missile) fire a second projectile the same shot. */
    if (vy2 != JAZZ_NO_SECOND) spawn_bullet(g, vx, vy2, gravity, behaviour);
    g->fireCooldown = 10;
    g->events |= JAZZ_EVENT_FIRE;
}

static void kill_enemy(JazzGame *g, JazzEnemy *e)
{
    e->active = 0;
    g->score += 100;
    g->events |= JAZZ_EVENT_KILL;
#ifdef JAZZ_JJ1_RUNTIME
    mark_event_taken(g, e->gridX, e->gridY);
#endif
}

#ifdef JAZZ_JJ1_RUNTIME
static void destroy_cell(JazzGame *g, u8 gridX, u8 gridY)
{
    u16 bit = ((u16)gridY << 8) + gridX;
    g->destroyed[bit >> 3] |= (u8)(1 << (bit & 7));
    /* Tell the renderer which block to repaint as empty. */
    g->destroyedX = gridX;
    g->destroyedY = gridY;
    g->destroyCount++;
}

/* Register a shot against a destructible cell.  The original counts hits per
 * cell and swaps the block once they reach the event's strength. */
static void hit_destructible(JazzGame *g, u8 gridX, u8 gridY, u8 strength)
{
    u8 i, free_slot = JAZZ_MAX_DESTRUCT_HITS;
    if (strength <= 1) { destroy_cell(g, gridX, gridY); return; }
    for (i = 0; i < JAZZ_MAX_DESTRUCT_HITS; i++) {
        JazzDestructHit *d = &g->destructHits[i];
        if (d->active && (d->gridX == gridX) && (d->gridY == gridY)) {
            d->hits++;
            if (d->hits >= strength) { d->active = 0; destroy_cell(g, gridX, gridY); }
            return;
        }
        if (!d->active && (free_slot == JAZZ_MAX_DESTRUCT_HITS)) free_slot = i;
    }
    if (free_slot == JAZZ_MAX_DESTRUCT_HITS) free_slot = 0; /* reuse oldest */
    g->destructHits[free_slot].gridX = gridX;
    g->destructHits[free_slot].gridY = gridY;
    g->destructHits[free_slot].hits = 1;
    g->destructHits[free_slot].active = 1;
}

/* A bullet that stopped at (x,y): if it struck a destructible block that is
 * still standing, damage it. */
static void grant_item(JazzGame *g, const Jj1EventInfo *info, u8 gridX, u8 gridY);

static void bullet_hits_scenery(JazzGame *g, s16 x, s16 y)
{
    s16 gx = x >> 5, gy = y >> 5;
    const Jj1EventInfo *info;
    if ((gx < 0) || (gx > 255) || (gy < 0) || (gy > 63)) return;
    if (jazz_cell_destroyed(g, (u8)gx, (u8)gy)) return;
    info = jj1_event_info(g->stage, jj1_runtime_event(g->stage, gx, gy));
    /* An end sign with strength (Medivo's is strength 1) is finished by
       shooting it: hitEvent routes the last hit through takeEvent, whose
       case 27 ends the level.  Touch cannot reach it there, so without this
       the level cannot be completed at all. */
    if ((info->klass == JJ1_CLASS_END) && !g->transitionTimer) {
        g->transitionTimer = 75;
        g->events |= JAZZ_EVENT_EXIT;
        return;
    }
    /* A pickup with strength is a crate: shooting it open is the only way to
       take it, and the contents are granted exactly as on a touch pickup. */
    if ((info->klass == JJ1_CLASS_ITEM) && info->strength) {
        if (!jazz_event_taken(g, (u8)gx, (u8)gy)) grant_item(g, info, (u8)gx, (u8)gy);
        return;
    }
    if (info->klass != JJ1_CLASS_DESTRUCT) return;
    hit_destructible(g, (u8)gx, (u8)gy, info->strength);
}
#endif

/* The bird companion trails the player and fires at enemies near it, as
 * JJ1Bird::step does: it closes on the leader, hovers slightly above, and
 * shoots when an enemy is within range on the side the player faces. */
static void update_bird(JazzGame *g)
{
    JazzPlayer *p = &g->player;
    s16 targetX, targetY;
    u8 e;
    if (!p->bird) return;

    targetX = (s16)(p->facing ? (p->x - 20) : (p->x + 20));
    targetY = (s16)(p->y - 20);
    /* Ease toward the trailing position rather than snapping to it. */
    p->birdX = (s16)(p->birdX + ((targetX - p->birdX) >> 3));
    p->birdY = (s16)(p->birdY + ((targetY - p->birdY) >> 3));

    if (p->birdCooldown) { p->birdCooldown--; return; }
    for (e = 0; e < JAZZ_MAX_ENEMIES; e++) {
        JazzEnemy *en = &g->enemies[e];
        s16 dx;
        if (!en->active) continue;
        if ((en->klass != JJ1_CLASS_ENEMY_WALK) && (en->klass != JJ1_CLASS_ENEMY_FLY)) continue;
        dx = (s16)(en->x - p->birdX);
        if (p->facing ? (dx < 0) : (dx > 0)) continue;   /* only ahead of the player */
        if (dx < 0) dx = (s16)-dx;
        if (dx > 160) continue;
        {
            s16 dy = (s16)(en->y - p->birdY);
            if (dy < 0) dy = (s16)-dy;
            if (dy > 48) continue;
        }
        /* The bird fires bullet 30, whose definition holds BOTH speed pairs
           (xspeed +-2 and +-3, yspeed -1 and -2, gravity): two shots leave at
           once and arc downward, which is what lets it drop fire onto enemies
           the flat bolt used to sail over. */
        {
            static const s16 birdVx[2] = { 1400, 1700 };
            static const s16 birdVy[2] = { -250, -500 };
            u8 shot;
            for (shot = 0; shot < 2; shot++) {
                u8 i;
                for (i = 0; i < JAZZ_MAX_BULLETS; i++) {
                    JazzBullet *b = &g->bullets[i];
                    if (b->active) continue;
                    b->active = 1;
                    b->x = p->birdX;
                    b->y = p->birdY;
                    b->vx = (s16)(p->facing ? birdVx[shot] : -birdVx[shot]);
                    b->vy = birdVy[shot];
                    b->subX = 0;
                    b->subY = 0;
                    b->gravity = 24;     /* same scale the bouncer uses */
                    b->behaviour = 0;
                    p->birdCooldown = 30;
                    g->events |= JAZZ_EVENT_FIRE;
                    break;
                }
            }
        }
        break;
    }
}

static void update_bullets(JazzGame *g)
{
    u8 i, e;
    JazzBullet *b;
    for (i = 0; i < JAZZ_MAX_BULLETS; i++) {
        s8 dx, dy;
        b = &g->bullets[i];
        if (!b->active) continue;

        /* Gravity acts on arcing shots (the bouncer). */
        if (b->gravity) b->vy = (s16)(b->vy + b->gravity);

        dx = consume_subpixels(b->vx, &b->subX);
        dy = consume_subpixels(b->vy, &b->subY);
        b->x = (s16)(b->x + dx);
        b->y = (s16)(b->y + dy);

#ifdef JAZZ_JJ1_RUNTIME
        /* Crates are events, not solid tiles, so a shot has to be tested
           against the event grid too or it flies straight through them. */
        {
            s16 cgx = b->x >> 5, cgy = b->y >> 5;
            if ((cgx >= 0) && (cgx < 256) && (cgy >= 0) && (cgy < 64)) {
                const Jj1EventInfo *ci =
                    jj1_event_info(g->stage, jj1_runtime_event(g->stage, cgx, cgy));
                if ((ci->klass == JJ1_CLASS_ITEM) && ci->strength &&
                    !jazz_event_taken(g, (u8)cgx, (u8)cgy)) {
                    grant_item(g, ci, (u8)cgx, (u8)cgy);
                    b->active = 0;
                    continue;
                }
            }
        }
#endif
        if (rect_hits_solid(g, b->x, b->y, 4, 4)) {
            if (b->behaviour == 4) {
                /* Bouncer: reflect off the surface it struck instead of dying.
                   Undo the step, flip the velocity component that hit, and damp
                   it slightly so the bounce loses energy like the original. */
                b->x = (s16)(b->x - dx);
                b->y = (s16)(b->y - dy);
                if (rect_hits_solid(g, (s16)(b->x + dx), b->y, 4, 4)) b->vx = (s16)-b->vx;
                if (rect_hits_solid(g, b->x, (s16)(b->y + dy), 4, 4)) {
                    b->vy = (s16)(-b->vy - (b->vy >> 2));
                    if (b->vy > -200 && b->vy < 200) b->vy = -400; /* keep it lively */
                }
            } else {
                b->active = 0;
#ifdef JAZZ_JJ1_RUNTIME
                /* Probe slightly ahead: the bullet stops at the block's edge. */
                bullet_hits_scenery(g, (s16)(b->x + ((b->vx > 0) ? 3 : 0)), (s16)(b->y + 2));
#endif
                continue;
            }
        }
        if ((b->x < g->player.x - 400) || (b->x > g->player.x + 400)) { b->active = 0; continue; }
        for (e = 0; e < JAZZ_MAX_ENEMIES; e++) {
            JazzEnemy *en = &g->enemies[e];
            if (!en->active) continue;
#ifdef JAZZ_JJ1_RUNTIME
            if (en->klass == JJ1_CLASS_HAZARD) continue; /* not shootable */
#endif
            if (rect_overlap(b->x, b->y, 4, 4, en->x, en->y, ENEMY_W, ENEMY_H)) {
                b->active = 0;
                if (en->hitPoints > 1) en->hitPoints--;
                else kill_enemy(g, en);
                break;
            }
        }
    }
}

#ifdef JAZZ_JJ1_RUNTIME

/* Sine-ish bob table for flyers, amplitude 16px over 64 phases. */
static const s8 jj1_bob[64] = {
      0,  2,  3,  5,  6,  8,  9, 10, 12, 13, 14, 14, 15, 16, 16, 16,
     16, 16, 16, 15, 14, 14, 13, 12, 10,  9,  8,  6,  5,  3,  2,  0,
      0, -2, -3, -5, -6, -8, -9,-10,-12,-13,-14,-14,-15,-16,-16,-16,
    -16,-16,-16,-15,-14,-14,-13,-12,-10, -9, -8, -6, -5, -3, -2,  0
};

static void spawn_grid_enemy(JazzGame *g, u8 gx, u8 gy, u8 klass, u8 strength)
{
    u8 i;
    JazzEnemy *e = 0;
    for (i = 0; i < JAZZ_MAX_ENEMIES; i++) {
        if (g->enemies[i].active) {
            if ((g->enemies[i].gridX == gx) && (g->enemies[i].gridY == gy)) return;
        } else if (!e) e = &g->enemies[i];
    }
    if (!e) return;
    e->active = 1;
    e->klass = klass;
    e->gridX = gx;
    e->gridY = gy;
    e->homeX = ((s16)gx << 5) + ((32 - ENEMY_W) >> 1);
    e->homeY = ((s16)gy << 5) + (32 - ENEMY_H);
    e->x = e->homeX;
    e->y = e->homeY;
    e->direction = -1;
    e->hitPoints = strength ? strength : 1;
    e->phase = (u8)((gx * 7) & 63);
    if (klass == JJ1_CLASS_ENEMY_WALK) {
        /* settle onto the mask below the event cell */
        while ((e->y < (WORLD_H - ENEMY_H - 1)) &&
               !down_pt_solid(g, e->x + (ENEMY_W >> 1), e->y + ENEMY_H)) e->y++;
        e->homeY = e->y;
    }
}

/* Activate enemies whose event cell enters the streaming window around the
 * player; deactivate those far outside it.  One column is scanned per frame
 * to bound the 68000 cost. */
#define JJ1_ACTIVATE_MARGIN_X 208
#define JJ1_ACTIVATE_MARGIN_Y 160
#define JJ1_DEACTIVATE_MARGIN 288

static void scan_grid_enemies(JazzGame *g)
{
    s16 px = g->player.x;
    s16 py = g->player.y;
    s16 gx0 = (px - JJ1_ACTIVATE_MARGIN_X) >> 5;
    s16 gx1 = (px + JJ1_ACTIVATE_MARGIN_X) >> 5;
    s16 gy0 = (py - JJ1_ACTIVATE_MARGIN_Y) >> 5;
    s16 gy1 = (py + JJ1_ACTIVATE_MARGIN_Y) >> 5;
    s16 gx, gy;
    u8 i;

    if (gx0 < 0) gx0 = 0;
    if (gy0 < 0) gy0 = 0;
    if (gx1 > 255) gx1 = 255;
    if (gy1 > 63) gy1 = 63;

    /* round-robin: one activation column per frame */
    gx = gx0 + (s16)(g->enemyScanColumn % (u16)(gx1 - gx0 + 1));
    g->enemyScanColumn++;
    for (gy = gy0; gy <= gy1; gy++) {
        const Jj1EventInfo *info = jj1_event_info(g->stage, jj1_runtime_event(g->stage, gx, gy));
        if (((info->klass == JJ1_CLASS_ENEMY_WALK) ||
             (info->klass == JJ1_CLASS_ENEMY_FLY) ||
             (info->klass == JJ1_CLASS_HAZARD)) &&
            !jazz_event_taken(g, (u8)gx, (u8)gy))
            spawn_grid_enemy(g, (u8)gx, (u8)gy, info->klass, info->strength);
    }

    for (i = 0; i < JAZZ_MAX_ENEMIES; i++) {
        JazzEnemy *e = &g->enemies[i];
        if (!e->active) continue;
        if ((e->x < px - JJ1_DEACTIVATE_MARGIN) || (e->x > px + JJ1_DEACTIVATE_MARGIN) ||
            (e->y < py - JJ1_DEACTIVATE_MARGIN) || (e->y > py + JJ1_DEACTIVATE_MARGIN))
            e->active = 0; /* despawn without marking taken: it respawns on return */
    }
}

/* Apply a pickup's effect.  Shared by the two ways the original grants one:
 * walking into a zero-strength event, and shooting a crate open (whose
 * contents takeEvent hands over once the hits are spent). */
static void grant_item(JazzGame *g, const Jj1EventInfo *info, u8 gridX, u8 gridY)
{
    u8 kind = (u8)(info->param & 0x0F);
    mark_event_taken(g, gridX, gridY);
    g->stageGems++;
    g->score += (u16)info->points * 25;
    g->events |= JAZZ_EVENT_GEM;
    switch (kind) {
    case JJ1_ITEM_HEALTH:
        if (g->health < 5) g->health++;
        g->events |= JAZZ_EVENT_HEALTH;
        break;
    case JJ1_ITEM_LIFE:
        if (g->lives < 9) g->lives++;
        g->events |= JAZZ_EVENT_LIFE;
        break;
    case JJ1_ITEM_AMMO: {
        /* The weapon crate carries the shot type in the high nibble.  As in
           Player::addAmmo, the weapon is only auto-selected when the player had
           none of it: topping up a weapon you already carry must not yank you
           off the one you are using. */
        u8 shot = (u8)((info->param >> 4) & 0x0F);
        if (shot && (shot < JAZZ_SHOT_TYPES)) {
            u16 total;
            if (!g->player.ammo[shot]) g->player.shotType = shot;
            g->player.weaponsOwned |= (u8)(1 << shot);
            total = (u16)(g->player.ammo[shot] + JAZZ_AMMO_PER_CRATE);
            g->player.ammo[shot] = (u8)((total > 255) ? 255 : total);
        }
        break;
    }
    case JJ1_ITEM_INVINCIBLE:
        /* Temporary invincibility, as reaction PR_INVINCIBLE in the original. */
        g->player.invincibleTime = JAZZ_INVINCIBLE_FRAMES;
        break;
    case JJ1_ITEM_AIRBOARD:
        /* The airboard flies until the level ends or a modifier-38 event. */
        g->player.flying = 1;
        break;
    case JJ1_ITEM_BIRD:
        g->player.bird = 1;
        g->player.birdX = g->player.x;
        g->player.birdY = (s16)(g->player.y - 24);
        break;
    case JJ1_ITEM_SHIELD:
        /* Capacity (1 or 4 hits) rides in the param high nibble. */
        g->player.shield = (u8)((info->param >> 4) & 0x0F);
        break;
    case JJ1_ITEM_FASTFEET:
        g->player.fastFeetTime = JAZZ_FASTFEET_FRAMES;
        break;
    case JJ1_ITEM_HIGHJUMP:
        /* High-jump feet raise the jump arc for the rest of the level. */
        g->player.highJump = 1;
        break;
    default:
        break;
    }
}

static void collect_grid_items(JazzGame *g)
{
    s16 gx0 = g->player.x >> 5;
    s16 gx1 = (g->player.x + PLAYER_W - 1) >> 5;
    s16 gy0 = g->player.y >> 5;
    s16 gy1 = (g->player.y + PLAYER_H - 1) >> 5;
    s16 gx, gy;
    for (gy = gy0; gy <= gy1; gy++) {
        for (gx = gx0; gx <= gx1; gx++) {
            const Jj1EventInfo *info;
            if ((gx < 0) || (gx > 255) || (gy < 0) || (gy > 63)) continue;
            info = jj1_event_info(g->stage, jj1_runtime_event(g->stage, gx, gy));
            if (info->klass == JJ1_CLASS_UNBOARD) {
                /* Modifier 38: "Airboard, etc. off".  The zone persists and
                   simply cancels flight on contact, as touchEvent case 38. */
                g->player.flying = 0;
                continue;
            }
            if (info->klass != JJ1_CLASS_ITEM) continue;
            if (jazz_event_taken(g, (u8)gx, (u8)gy)) continue;
            /* Only a zero-strength pickup is grabbed by walking into it; one
               with strength is a crate that has to be shot open first (the
               original's touchEvent only takes an event when !strength). */
            if (info->strength) continue;
            grant_item(g, info, (u8)gx, (u8)gy);
        }
    }
}

#endif /* JAZZ_JJ1_RUNTIME */

static void update_enemies(JazzGame *g)
{
    u8 i;
    JazzEnemy *e;
    for (i = 0; i < JAZZ_MAX_ENEMIES; i++) {
        e = &g->enemies[i];
        if (!e->active) continue;
#ifdef JAZZ_JJ1_RUNTIME
        if (e->klass == JJ1_CLASS_ENEMY_WALK) {
            s16 nx = e->x + e->direction;
            s16 footX = nx + ((e->direction > 0) ? (ENEMY_W - 1) : 0);
            /* Turn at walls and at ledges, like the original walkers. */
            if (pt_solid(g, footX, e->y + (ENEMY_H >> 1)) ||
                !down_pt_solid(g, footX, e->y + ENEMY_H + 2) ||
                (nx < e->homeX - 64) || (nx > e->homeX + 64))
                e->direction = -e->direction;
            else e->x = nx;
        } else if (e->klass == JJ1_CLASS_ENEMY_FLY) {
            e->phase = (e->phase + 1) & 63;
            e->y = e->homeY + jj1_bob[e->phase];
            e->x += e->direction;
            if ((e->x < e->homeX - 48) || (e->x > e->homeX + 48)) e->direction = -e->direction;
        }
        /* JJ1_CLASS_HAZARD stays still. */
#else
        e->x += e->direction;
        if ((e->x >> 4) <= (e->homeX >> 4)) e->direction = 1;
        if ((e->x >> 4) >= (e->homeY >> 4)) e->direction = -1;
#endif
        if (rect_overlap(g->player.x, g->player.y, PLAYER_W, PLAYER_H, e->x, e->y, ENEMY_W, ENEMY_H)) hurt_player(g);
    }
}

static void collect_gems(JazzGame *g)
{
#ifdef JAZZ_JJ1_RUNTIME
    collect_grid_items(g);
#else
    u8 i;
    for (i = 0; i < JAZZ_MAX_GEMS; i++) {
        if (g->gems[i].active && rect_overlap(g->player.x, g->player.y, PLAYER_W, PLAYER_H, g->gems[i].x, g->gems[i].y, 8, 12)) {
            g->gems[i].active = 0;
            g->stageGems++;
            g->score += 25;
            g->events |= JAZZ_EVENT_GEM;
        }
    }
#endif
}

static u8 player_touches_exit(const JazzGame *g)
{
#ifdef JAZZ_JJ1_RUNTIME
    return jj1_runtime_touches_end(g->stage, g->player.x, g->player.y, PLAYER_W, PLAYER_H);
#else
    s16 tx, ty;
    s16 tx0 = g->player.x >> 4;
    s16 tx1 = (g->player.x + PLAYER_W - 1) >> 4;
    s16 ty0 = g->player.y >> 4;
    s16 ty1 = (g->player.y + PLAYER_H - 1) >> 4;
    for (ty = ty0; ty <= ty1; ty++)
        for (tx = tx0; tx <= tx1; tx++)
            if (jazz_tile_at(g, tx, ty) == JAZZ_TILE_EXIT) return 1;
    return 0;
#endif
}

#ifdef JAZZ_JJ1_RUNTIME
void jazz_debug_place(JazzGame *g, s16 x, s16 y)
{
    g->player.x = x;
    g->player.y = y;
    g->player.subX = 0;
    g->player.subY = 0;
    g->player.vx = 0;
    g->player.vy = 0;
    g->player.jumpTargetY = JAZZ_NO_JUMP_TARGET;
    g->player.springJump = 0;
}

/* Test hook: apply one point of contact damage, going through the same shield
 * and invincibility handling the game uses. */
/* Test hook: apply a pickup's effect directly. */
void jazz_debug_grant(JazzGame *g, const Jj1EventInfo *info)
{
    grant_item(g, info, 0, 0);
}

/* Test hook: land a bullet on a grid cell, as update_bullets would. */
void jazz_debug_shoot_cell(JazzGame *g, s16 x, s16 y)
{
#ifdef JAZZ_JJ1_RUNTIME
    bullet_hits_scenery(g, x, y);
#else
    (void)g; (void)x; (void)y;
#endif
}

void jazz_debug_hurt(JazzGame *g)
{
    hurt_player(g);
}

void jazz_debug_set_stage(JazzGame *g, u8 stage)
{
    build_stage(g, stage);
}

s16 jazz_player_feet(const JazzGame *g)
{
    return (s16)(g->player.y + PLAYER_H);
}

/* Choose the animation state from physics, following the order the original
 * uses in JJ1LevelPlayer::animate: hurt/spring first, then airborne, then the
 * ground states (run/skid/shoot/crouch/lookup/walk/stand). */
u8 jazz_player_anim_state(const JazzGame *g, u16 input)
{
    const JazzPlayer *p = &g->player;
    s16 speed = (p->vx < 0) ? -p->vx : p->vx;

    if (p->flying) return JJ1_PLAYER_BOARD;
    if (g->invulnerability && !p->onGround) return JJ1_PLAYER_HURT;
    if (p->springJump) return JJ1_PLAYER_SPRING;

    if (!p->onGround) {
        if (p->vy < 0) return JJ1_PLAYER_JUMP;
        return JJ1_PLAYER_FALL;
    }

    /* Firing while grounded shows the shoot pose. */
    if (g->fireCooldown && speed < JAZZ_PXS_WALK) return JJ1_PLAYER_SHOOT;

    if (speed >= JAZZ_PXS_RUN) return JJ1_PLAYER_RUN;
    /* Pressing away from current motion is a skid/turn. */
    if (((p->vx > 0) && (input & JAZZ_INPUT_LEFT)) ||
        ((p->vx < 0) && (input & JAZZ_INPUT_RIGHT)))
        return JJ1_PLAYER_SKID;
    if (speed >= JAZZ_PXA_WALK) return JJ1_PLAYER_WALK;

    if (input & JAZZ_INPUT_JUMP) return JJ1_PLAYER_LOOKUP; /* up held: look up */
    return JJ1_PLAYER_STAND;
}
#endif

/* Horizontal control: two-tier acceleration toward walk speed, then run
 * speed while the direction is held; braking uses the stop deceleration and
 * turning applies the stronger reverse deceleration first. */
/* Top running speed, raised while fast feet are active (modifier 26). */
static s16 run_ceiling(const JazzPlayer *p)
{
    return p->fastFeetTime ? JAZZ_PXS_FASTRUN : JAZZ_PXS_RUN;
}

static void accelerate_player(JazzGame *g, u16 input)
{
    JazzPlayer *p = &g->player;
    if (input & JAZZ_INPUT_RIGHT) {
        p->facing = 1;
        if (p->vx < 0) p->vx += JAZZ_PXA_REVERSE;
        else if (p->vx < JAZZ_PXS_WALK) p->vx += JAZZ_PXA_WALK;
        else if (p->vx < run_ceiling(p)) p->vx += JAZZ_PXA_RUN;
        if (p->vx > run_ceiling(p)) p->vx = run_ceiling(p);
    } else if (input & JAZZ_INPUT_LEFT) {
        p->facing = 0;
        if (p->vx > 0) p->vx -= JAZZ_PXA_REVERSE;
        else if (p->vx > -JAZZ_PXS_WALK) p->vx -= JAZZ_PXA_WALK;
        else if (p->vx > -run_ceiling(p)) p->vx -= JAZZ_PXA_RUN;
        if (p->vx < -run_ceiling(p)) p->vx = -run_ceiling(p);
    } else {
        if (p->vx > 0) { if (p->vx < JAZZ_PXA_STOP) p->vx = 0; else p->vx -= JAZZ_PXA_STOP; }
        else if (p->vx < 0) { if (p->vx > -JAZZ_PXA_STOP) p->vx = 0; else p->vx += JAZZ_PXA_STOP; }
    }
}

/* Vertical control: target-height ascent as in the original.  While a jump
 * target is set and the button held, the ascent speed is recomputed from the
 * remaining height and clamped to the jump speed; releasing the button or
 * bonking a ceiling clears the target, and gravity only decays an upward
 * speed - a downward move immediately uses the terminal fall speed. */
static void integrate_vertical(JazzGame *g, u16 input)
{
    JazzPlayer *p = &g->player;

    /* Airboard: while flying the player thrusts up or down under direct
       control and gravity is suspended entirely, as in the original's flying
       branch (jump accelerates upward, crouch downward, both capped at run
       speed).  Jump doubles as "up" so the board works on a 3-button pad. */
    if (p->flying) {
        if (input & (JAZZ_INPUT_UP | JAZZ_INPUT_JUMP)) {
            if (p->vy > 0) p->vy -= JAZZ_PXA_REVERSE;
            else if (p->vy > -JAZZ_PXS_WALK) p->vy -= JAZZ_PXA_WALK;
            else if (p->vy > -JAZZ_PXS_RUN) p->vy -= JAZZ_PXA_RUN;
        } else if (input & JAZZ_INPUT_DOWN) {
            if (p->vy < 0) p->vy += JAZZ_PXA_REVERSE;
            else if (p->vy < JAZZ_PXS_WALK) p->vy += JAZZ_PXA_WALK;
            else if (p->vy < JAZZ_PXS_RUN) p->vy += JAZZ_PXA_RUN;
        } else {
            /* Coast back to level flight. */
            if (p->vy > JAZZ_PXA_WALK) p->vy -= JAZZ_PXA_WALK;
            else if (p->vy < -JAZZ_PXA_WALK) p->vy += JAZZ_PXA_WALK;
            else p->vy = 0;
        }
        p->jumpTargetY = JAZZ_NO_JUMP_TARGET;
        p->springJump = 0;
        return;
    }

    if ((input & JAZZ_INPUT_JUMP) && p->onGround &&
        (p->jumpTargetY == JAZZ_NO_JUMP_TARGET) &&
        !(g->previousInput & JAZZ_INPUT_JUMP)) {
        s16 bonus = p->vx;
        if (bonus < 0) bonus = -bonus;
        p->jumpTargetY = p->y - JAZZ_PYO_JUMP - (bonus >> 5); /* speed bonus, px */
        /* High-jump feet extend the arc for the rest of the level. */
        if (p->highJump) p->jumpTargetY -= JAZZ_HIGHJUMP_BONUS;
        p->springJump = 0;
        g->events |= JAZZ_EVENT_JUMP;
    }

    if (!(input & JAZZ_INPUT_JUMP) && p->onGround && !p->springJump)
        p->jumpTargetY = JAZZ_NO_JUMP_TARGET;

    if (p->jumpTargetY != JAZZ_NO_JUMP_TARGET) {
        if ((!(input & JAZZ_INPUT_JUMP) && !p->springJump) || (p->y <= p->jumpTargetY)) {
            p->jumpTargetY = JAZZ_NO_JUMP_TARGET;
            p->springJump = 0;
        } else {
            /* remaining = (y - target); ascent = -(remaining + 64)/16 px/f */
            s16 remaining = p->y - p->jumpTargetY;
            s32 want = -(((s32)remaining + 64) << 8) >> 4;
            if (want < JAZZ_PYS_JUMP) want = JAZZ_PYS_JUMP;
            p->vy = (s16)want;
            return;
        }
    }

    if (p->vy < 0) {
        p->vy += JAZZ_PYA_GRAVITY;
        if (p->vy >= 0) p->vy = JAZZ_PYS_FALL;
    } else if (!p->onGround) {
        p->vy = JAZZ_PYS_FALL;
    } else {
        p->vy = JAZZ_PYS_FALL; /* keeps feet pressed to ramps */
    }
}

/* Convert an 8.8 velocity plus a running signed remainder into pixels. */
static s8 consume_subpixels(s16 velocity, s16 *acc)
{
    s16 total = (s16)(velocity + *acc);
    s8 pixels = (s8)(total / 256); /* truncates toward zero for both signs */
    *acc = (s16)(total - ((s16)pixels << 8));
    return pixels;
}

void jazz_step(JazzGame *g, u16 input)
{
    u16 pressed = input & ~g->previousInput;
    s8 dx, dy;
    g->events = JAZZ_EVENT_NONE;
    g->frame++;

    if (pressed & JAZZ_INPUT_START) { g->previousInput = input; g->paused = !g->paused; return; }
    if (g->paused || g->finished) { g->previousInput = input; return; }

    if (g->transitionTimer) {
        g->previousInput = input;
        g->transitionTimer--;
        if (!g->transitionTimer) {
            if ((u8)(g->stage + 1) < JAZZ_STAGE_COUNT) build_stage(g, g->stage + 1);
            else g->finished = 1;
        }
        return;
    }

    accelerate_player(g, input);
#ifdef JAZZ_JJ1_RUNTIME
    {
        /* Sucker tubes.  A horizontal tube pushes left/right; a vertical tube
           launches the player upward to a target height, exactly like the
           original's REPELUP (which reuses the spring's target-height ascent).
           Modelling the launch as a jump target - rather than a fixed upward
           velocity - is what lets the player clear the shaft and fly out the
           top instead of sticking at the tube's last cell. */
        s16 pushX = 0, targetY = 0;
        u8 kind = jj1_runtime_tube_push(g->stage, g->player.x, g->player.y,
                                        PLAYER_W, PLAYER_H, &pushX, &targetY);
        g->player.inTube = (kind != 0);
        if (kind) {
            if (pushX) {
                s16 mag = (pushX < 0) ? -pushX : pushX;
                u8 against = ((pushX < 0) && (input & JAZZ_INPUT_RIGHT)) ||
                             ((pushX > 0) && (input & JAZZ_INPUT_LEFT));
                if (mag >= JAZZ_PXS_WALK || !against) {
                    g->player.vx = pushX;
                    if (pushX > 0) g->player.facing = 1;
                    else if (pushX < 0) g->player.facing = 0;
                }
            }
            if (kind == 2) {
                /* Vertical launch: aim for the target height if we are not
                   already above it.  springJump keeps the ascent going after
                   the player leaves the tube cell, and blocks the button check
                   from cancelling it. */
                if (g->player.y > targetY) {
                    g->player.jumpTargetY = targetY;
                    g->player.springJump = 1;
                }
            }
        }
    }
#endif
    integrate_vertical(g, input);
    g->previousInput = input;

    if (pressed & JAZZ_INPUT_SWITCH) {
        /* Cycle to the next weapon the player actually owns. */
        u8 next = g->player.shotType;
        u8 tries;
        for (tries = 0; tries < JAZZ_SHOT_TYPES; tries++) {
            next = (u8)((next + 1) % JAZZ_SHOT_TYPES);
            if (g->player.weaponsOwned & (u8)(1 << next)) {
                g->player.shotType = next;
                break;
            }
        }
    }
    if ((pressed & JAZZ_INPUT_FIRE) && !g->fireCooldown) fire_bullet(g);
    if (g->fireCooldown) g->fireCooldown--;
    /* Timed powerups wind down; the permanent ones (high jump, shield) do not. */
    if (g->player.invincibleTime) g->player.invincibleTime--;
    if (g->player.fastFeetTime) g->player.fastFeetTime--;
    if (g->invulnerability) g->invulnerability--;

    dx = consume_subpixels(g->player.vx, &g->player.subX);
    if (dx) move_player_x(g, dx);
    dy = consume_subpixels(g->player.vy, &g->player.subY);
    if (dy) move_player_y(g, dy);
    else if (g->player.vy > 0) {
        /* sub-pixel fall: still verify ground contact */
        g->player.onGround = player_vertical_blocked(g, g->player.x, g->player.y + 1, 1);
    }

#ifdef JAZZ_JJ1_RUNTIME
    {
        s16 springTop = 0;
        u16 height = jj1_runtime_spring_height(g->stage, g->player.x, g->player.y,
                                               PLAYER_W, PLAYER_H, &springTop);
        if (height) {
            /* Springs retarget the shared ascent rule, as in the original:
             * the FEET rise to `height` px above the spring block (the
             * player top therefore targets PLAYER_H higher), and the ascent
             * proceeds regardless of the jump button. */
            g->player.jumpTargetY = springTop - (s16)height - PLAYER_H;
            g->player.springJump = 1;
            g->player.vy = JAZZ_PYS_JUMP;
            g->player.onGround = 0;
            g->events |= JAZZ_EVENT_JUMP;
        }
    }
    scan_grid_enemies(g);
#endif

    update_bird(g);
    update_bullets(g);
    update_enemies(g);
    collect_gems(g);

    if (player_touches_exit(g)) {
        g->transitionTimer = 75;
        g->events |= JAZZ_EVENT_EXIT;
    }
}
