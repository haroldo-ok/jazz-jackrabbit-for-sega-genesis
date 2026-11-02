#include "bonus.h"
#include "jj1_bonus_data.inc"

#define F1   1024
#define FQ   256
#define FH   512
#define F12  12288
#define F16  16384
#define F20  20480
#define ITOF(x) ((s32)(x) << 10)
#define MUL(x, y) ((s32)(((s32)(x) * (s32)(y)) >> 10))

/* Player constants from the original (fixed point). */
#define PRS_CRASH   (-ITOF(60))
#define PRS_REVERSE (-ITOF(30))
#define PRS_JUMP    ITOF(40)
#define PRS_WALK    ITOF(40)
#define PRS_RUN     ITOF(80)
#define PAS_TURN    384
#define PZS_FALL    (-ITOF(2))
#define PZS_JUMP    ITOF(3)
#define PRA_REBOUND 150
#define PRA_REVERSE 450
#define PRA_STOP    200
#define PRA_WALK    100
#define PAA_STOP    6
#define PAA_TURN    1
#define PZA_GRAVITY (-6)

s32 bonus_sin(s32 angle) { return jj1_bonus_sin[angle & 1023]; }
s32 bonus_cos(s32 angle) { return jj1_bonus_sin[(angle + 256) & 1023]; }

void bonus_init(BonusGame *b, u8 level, const u8 *floorTiles, const u8 *masks,
                const u8 *grid, const u8 *events)
{
    u8 i;
    if (level >= BONUS_LEVELS) level = 0;
    b->floorTiles = floorTiles; b->masks = masks; b->grid = grid; b->events = events;
    for (i = 0; i < 16; i++) {
        b->evType[i] = jj1_bonus_meta[level].type[i];
        b->evPass[i] = jj1_bonus_meta[level].passable[i];
        b->evUsed[i] = jj1_bonus_meta[level].used[i];
        b->evSound[i] = jj1_bonus_meta[level].sound[i];
    }
    b->items = jj1_bonus_meta[level].items;
    b->endTime = (s32)jj1_bonus_meta[level].seconds * 1000;
    b->ticks = BONUS_STEP_MS;
    b->x = ((s32)jj1_bonus_meta[level].startX << 15) + F16;
    b->y = ((s32)jj1_bonus_meta[level].startY << 15) + F16;
    b->z = 0; b->dz = 0; b->dr = 0; b->da = 0;
    b->direction = FQ;
    b->running = 0; b->crashed = 0; b->jumping = 0;
    b->gems = 0; b->removedCount = 0; b->sound = 0; b->gotTime = 0;
}

static int is_removed(const BonusGame *b, u16 cell)
{
    u8 i;
    for (i = 0; i < b->removedCount; i++) if (b->removed[i] == cell) return 1;
    return 0;
}

u8 bonus_event(const BonusGame *b, int tx, int ty)
{
    u16 cell = (u16)(((ty & 255) << 8) | (tx & 255));
    u8 ev = b->events[cell];
    if (ev && is_removed(b, cell)) return 0;
    return ev;
}

static int in_event_area(s32 x, s32 y)
{
    return ((x & 32767) > F12) && ((x & 32767) < F20) &&
           ((y & 32767) > F12) && ((y & 32767) < F20);
}

int bonus_blocked(const BonusGame *b, s32 x, s32 y)
{
    int tx = (int)(x >> 15) & 255, ty = (int)(y >> 15) & 255;
    u8 ev = bonus_event(b, tx, ty);
    u8 tile = b->grid[(ty << 8) | tx];
    if (tile > 59) tile = 59;
    if (b->evUsed[ev & 15] && !b->evPass[ev & 15] && ev && in_event_area(x, y)) return 1;
    return b->masks[(tile << 6) + (((y >> 9) & 56) + ((x >> 12) & 7))];
}

static void player_step(BonusGame *b, u8 in)
{
    const s32 msps = BONUS_STEP_MS;
    s32 cdx, cdy;

    if (b->crashed) {
        if (b->dr < 0) b->dr += PRA_REBOUND * msps;
        else { b->dr = 0; b->crashed = 0; }
    } else {
        if (in & BONUS_INPUT_RUN) { b->running = 1; b->dr = PRS_RUN; }
        if (in & BONUS_INPUT_UP) {
            if (b->dr < 0) b->dr += PRA_REVERSE * msps;
            else if (b->dr < PRS_WALK) b->dr += PRA_WALK * msps;
        } else if (in & BONUS_INPUT_DOWN) {
            b->running = 0;
            if (b->dr > 0) b->dr -= PRA_REVERSE * msps;
            else if (b->dr > PRS_REVERSE) b->dr -= PRA_WALK * msps;
        } else {
            if (!b->running && b->dr > 0) {
                if (b->dr < PRA_STOP * msps) b->dr = 0; else b->dr -= PRA_STOP * msps;
            }
            if (b->dr < 0) {
                if (b->dr > -PRA_STOP * msps) b->dr = 0; else b->dr += PRA_STOP * msps;
            }
            if (b->dr == 0) b->running = 0;
        }
        if (in & BONUS_INPUT_LEFT) {
            if (b->da > -PAS_TURN) b->da -= PAA_TURN * msps;
        } else if (in & BONUS_INPUT_RIGHT) {
            if (b->da < PAS_TURN) b->da += PAA_TURN * msps;
        } else {
            if (b->da > 0) { if (b->da < PAA_STOP * msps) b->da = 0; else b->da -= PAA_STOP * msps; }
            if (b->da < 0) { if (b->da > -PAA_STOP * msps) b->da = 0; else b->da += PAA_STOP * msps; }
        }
        if (b->z == 0 && (in & BONUS_INPUT_JUMP)) {
            b->dz = PZS_JUMP;
            if (b->dr < PRS_JUMP) b->dr = PRS_JUMP;
        }
        if (b->dz > PZS_FALL) b->dz += PZA_GRAVITY * msps;
    }

    b->direction += (b->da * msps) >> 10;
    cdx = (MUL(bonus_sin(b->direction), b->dr) * msps) >> 10;
    cdy = (MUL(-bonus_cos(b->direction), b->dr) * msps) >> 10;
    if (!bonus_blocked(b, b->x + cdx, b->y)) b->x += cdx;
    if (!bonus_blocked(b, b->x, b->y + cdy)) b->y += cdy;
    b->z += (b->dz * msps) >> 10;
    if (b->z > F1) b->z = F1;
    if (b->z < 0) b->z = 0;
    b->jumping = (b->z > 0);

    if (b->running && bonus_blocked(b, b->x + cdx, b->y + cdy)) {
        b->running = 0; b->dr = PRS_CRASH; b->da = 0; b->crashed = 1;
    }
}

static void add_time(BonusGame *b, int seconds)
{
    b->endTime += (s32)seconds * 1000;
    if (b->endTime >= b->ticks + 10 * 60 * 1000) b->endTime = b->ticks + 10 * 60 * 1000 - 1;
}

int bonus_step(BonusGame *b, u8 input)
{
    s32 px, py;
    int tx, ty;
    u8 ev;
    b->sound = 0; b->gotTime = 0;
    b->ticks += BONUS_STEP_MS;
    if (b->ticks > b->endTime) { b->sound = 4; return BONUS_LOST; }   /* "ow" */

    px = b->x; py = b->y;                       /* events use the pre-step position */
    player_step(b, input);

    if (b->z < FH && in_event_area(px, py)) {
        tx = (int)(px >> 15) & 255; ty = (int)(py >> 15) & 255;
        ev = bonus_event(b, tx, ty);
        if (ev) {
            b->sound = b->evSound[ev & 15];
            switch (b->evType[ev & 15]) {
            case 1:   /* extra time */
                add_time(b, 60); b->gotTime = 1;
                if (b->removedCount < BONUS_MAX_REMOVED)
                    b->removed[b->removedCount++] = (u16)((ty << 8) | tx);
                break;
            case 2:   /* gem */
                b->gems++;
                if (b->removedCount < BONUS_MAX_REMOVED)
                    b->removed[b->removedCount++] = (u16)((ty << 8) | tx);
                if (b->gems >= b->items) return BONUS_WON;
                break;
            case 7:   /* exit */
                return BONUS_LOST;
            default:
                break;
            }
        }
    }
    return BONUS_CONTINUE;
}
