#include <genesis.h>
#include "jazz_game.h"
#include "jazz_gfx.h"
#include "jj1_events.h"
#include "jj1_runtime.h"
#include "jj1_sounds.h"

#define SCREEN_W 320
#define PLAYER_W 14
#define PLAYER_H 22

typedef enum { MODE_TITLE, MODE_PLAY, MODE_GAMEOVER, MODE_COMPLETE } GameMode;

static JazzGame game;
static GameMode mode;
static s16 cameraX;
static s16 cameraY;
static s16 lastMapColumn;
static u8 renderedStage;
static u16 renderedDestroyCount;
/* Which (event, frame) each enemy VRAM slot currently holds; 0xFFFF = unknown. */
static u16 enemySlotArt[JAZZ_MAX_ENEMIES];
static u8 playerSlotFrame;
static u8 selectedStage;   /* level select on the title screen */
static u16 titlePreviousPad;
static u8 sfxTimer;


static u16 input_from_pad(u16 pad)
{
    u16 input = 0;
    if (pad & BUTTON_LEFT) input |= JAZZ_INPUT_LEFT;
    if (pad & BUTTON_RIGHT) input |= JAZZ_INPUT_RIGHT;
    if (pad & (BUTTON_B | BUTTON_C)) input |= JAZZ_INPUT_JUMP;
    if (pad & BUTTON_A) input |= JAZZ_INPUT_FIRE;
    if (pad & BUTTON_START) input |= JAZZ_INPUT_START;
    return input;
}

static void number_to_text(char *out, u16 value)
{
    char reverse[6];
    u8 count = 0;
    u8 i;
    if (!value) { out[0] = '0'; out[1] = 0; return; }
    while (value && (count < 5)) { reverse[count++] = (char)('0' + (value % 10)); value /= 10; }
    for (i = 0; i < count; i++) out[i] = reverse[count - 1 - i];
    out[count] = 0;
}

/* The HUD lives on the window plane over the top two rows, and this runs
 * right after vblank - i.e. while the beam is drawing exactly those rows.
 * Clearing and rewriting them every frame therefore raced the raster: when
 * a streamed block column delayed the redraw, the beam passed rows 0-1 while
 * they were still blank and the HUD flickered out. So: never clear, build
 * fixed-width space-padded lines, and only touch VRAM when a line actually
 * changes (score/gems/health move rarely, so the window is stable). */
static char hudShown[2][41];

static void hud_pad(char *line, u8 pos)
{
    while (pos < 40) line[pos++] = ' ';
    line[40] = 0;
}

static void hud_commit(u8 row, const char *line)
{
    u8 i;
    for (i = 0; i < 41; i++) {
        if (hudShown[row][i] == line[i]) continue;
        /* Content changed: rewrite this row once and remember it. */
        for (i = 0; i < 41; i++) hudShown[row][i] = line[i];
        VDP_drawTextBG(WINDOW, line, 0, row);
        return;
    }
}

static void draw_status_line(const JazzGame *g)
{
    char line[41];
    char number[6];
    u8 pos = 0;
    u8 i;
    const char *prefix = "LIVES ";
    const char *gems = "  GEMS ";
    const char *score = "  SCORE ";

    while (*prefix) line[pos++] = *prefix++;
    number_to_text(number, g->lives); for (i = 0; number[i]; i++) line[pos++] = number[i];
    while (*gems) line[pos++] = *gems++;
    number_to_text(number, g->stageGems); for (i = 0; number[i]; i++) line[pos++] = number[i];
    line[pos++] = '/'; number_to_text(number, g->stageGemTotal); for (i = 0; number[i]; i++) line[pos++] = number[i];
    while (*score) line[pos++] = *score++;
    number_to_text(number, g->score); for (i = 0; number[i]; i++) line[pos++] = number[i];
    hud_pad(line, pos);
    hud_commit(0, line);

    pos = 0;
    if (g->paused) {
        const char *text = "        PAUSED - START RESUMES";
        while (*text) line[pos++] = *text++;
    } else if (g->transitionTimer) {
        const char *text = "              EXIT FOUND!";
        while (*text) line[pos++] = *text++;
    } else {
        /* Keep text as ROM literals. A 68000 traps on the unaligned long
           stores that recent GCC can otherwise use for a stack string. */
        static const char *const healthText[6] = {
            "HP:      ", "HP:*     ", "HP:**    ",
            "HP:***   ", "HP:****  ", "HP:***** "
        };
        const char *text = healthText[(g->health < 6) ? g->health : 5];
        const char *keys = " A FIRE  B/C JUMP  START PAUSE";
        while (*text) line[pos++] = *text++;
        while (*keys) line[pos++] = *keys++;
    }
    hud_pad(line, pos);
    hud_commit(1, line);
}

/* Force both rows to be re-sent after the window contents are destroyed
 * (title/end screens repaint the whole window plane). */
static void hud_invalidate(void)
{
    hudShown[0][0] = 0;
    hudShown[1][0] = 0;
    hudShown[0][1] = 1; /* differs from any real padded line */
    hudShown[1][1] = 1;
}



static void render_level_map(void)
{
    /* The foreground is populated directly by render_backdrop() from the
       decoded LEVEL/BLOCKS chunk. Keep the gameplay camera anchored until the
       full 256x64 chunk streamer replaces this resident screen. */
    cameraX = 0;
    lastMapColumn = 0;
    VDP_setHorizontalScroll(BG_A, 0);
}

static u8 jj1BlockCache[JJ1_BLOCK_CACHE_BLOCKS];
static u8 jj1CacheReplace;
static s16 jj1MapOriginX = -1;
static s16 jj1MapOriginY = -1;
static const Jj1StageArt *stageArt;
static const u32 *jj1ActiveBlockTiles;
static const u8 *jj1ActiveBlocks;

static void reset_jj1_cache(void)
{
    u16 i;
    for (i = 0; i < JJ1_BLOCK_CACHE_BLOCKS; i++) jj1BlockCache[i] = 0xFF;
    jj1CacheReplace = 0;
    jj1MapOriginX = -1;
    jj1MapOriginY = -1;
}

static u8 jj1_source_visible(u8 sourceBlock)
{
    u16 bx, by;
    for (by = 0; by < 8; by++)
        for (bx = 0; bx < 11; bx++)
            if (jj1ActiveBlocks[((jj1MapOriginY + by) * 256) + jj1MapOriginX + bx] == sourceBlock)
                return TRUE;
    return FALSE;
}

static u16 cache_jj1_block(u8 sourceBlock)
{
    u16 slot;
    for (slot = 0; slot < JJ1_BLOCK_CACHE_BLOCKS; slot++)
        if (jj1BlockCache[slot] == sourceBlock) return slot;

    /* Replace only a block which left the 11x8 visible window. This avoids
       rewriting visible VRAM tiles while the plane scrolls. */
    for (slot = 0; slot < JJ1_BLOCK_CACHE_BLOCKS; slot++)
        if ((jj1BlockCache[slot] == 0xFF) || !jj1_source_visible(jj1BlockCache[slot])) break;
    if (slot == JJ1_BLOCK_CACHE_BLOCKS) {
        slot = jj1CacheReplace++;
        if (jj1CacheReplace == JJ1_BLOCK_CACHE_BLOCKS) jj1CacheReplace = 0;
    }
    jj1BlockCache[slot] = sourceBlock;
    VDP_loadTileData(jj1ActiveBlockTiles + ((u32) sourceBlock * JJ1_BLOCK_TILE_COUNT * 8),
        T_JJ1_BLOCK_CACHE + (slot * JJ1_BLOCK_TILE_COUNT), JJ1_BLOCK_TILE_COUNT, DMA);
    return slot;
}

static void draw_jj1_block(s16 sourceX, s16 sourceY)
{
    u16 tx, ty;
    u8 sourceBlock;
    u16 baseTile;
    if ((sourceX < 0) || (sourceX >= 256) || (sourceY < 0) || (sourceY >= 64)) return;
    sourceBlock = jj1ActiveBlocks[(sourceY * 256) + sourceX];
    /* A destructible that has been shot away shows the backdrop through it,
       matching the engine swapping the block out (setTile). */
    if (jazz_cell_destroyed(&game, (u8)sourceX, (u8)sourceY)) sourceBlock = 0;
    baseTile = T_JJ1_BLOCK_CACHE + (cache_jj1_block(sourceBlock) * JJ1_BLOCK_TILE_COUNT);
    for (ty = 0; ty < 4; ty++)
        for (tx = 0; tx < 4; tx++)
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL2, FALSE, FALSE, FALSE,
                baseTile + (ty * 4) + tx), ((sourceX & 15) * 4) + tx, ((sourceY & 7) * 4) + ty);
}

static void render_jj1_map(s16 originX, s16 originY)
{
    s16 oldX, oldY;
    u16 bx, by;
    if (originX < 0) originX = 0;
    if (originX > (256 - 11)) originX = 256 - 11;
    if (originY < 0) originY = 0;
    if (originY > (64 - 8)) originY = 64 - 8;
    if ((originX == jj1MapOriginX) && (originY == jj1MapOriginY)) return;

    oldX = jj1MapOriginX;
    oldY = jj1MapOriginY;
    jj1MapOriginX = originX;
    jj1MapOriginY = originY;

    if ((oldX < 0) || ((originX - oldX > 1) || (oldX - originX > 1)) ||
        ((originY - oldY > 1) || (oldY - originY > 1))) {
        VDP_clearPlane(BG_A, TRUE);
        for (by = 0; by < 8; by++)
            for (bx = 0; bx < 11; bx++) draw_jj1_block(originX + bx, originY + by);
        return;
    }

    /* Ring-map update: only the new 32px block row/column is uploaded. */
    if (originX > oldX)
        for (by = 0; by < 8; by++) draw_jj1_block(originX + 10, originY + by);
    else if (originX < oldX)
        for (by = 0; by < 8; by++) draw_jj1_block(originX, originY + by);
    if (originY > oldY)
        for (bx = 0; bx < 11; bx++) draw_jj1_block(originX + bx, originY + 7);
    else if (originY < oldY)
        for (bx = 0; bx < 11; bx++) draw_jj1_block(originX + bx, originY);
}

static void render_jj1_sky(void)
{
    static const u8 bands[8] = {0, 1, 2, 4, 6, 7, 7, 7};
    u16 x, y;
    VDP_clearPlane(BG_B, TRUE);
    for (y = 0; y < 32; y++) {
        u16 tile = T_JJ1_SKY + bands[(y >> 2) & 7];
        for (x = 0; x < 64; x++)
            VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL3, FALSE, FALSE, FALSE, tile), x, y);
    }
}

static void render_backdrop(u8 stage)
{
    /* Everything that varies by stage comes from one table: the world's tile
       bank and palette, plus this level's sprite palette, Jazz frames, springs
       and event sprites. */
    stageArt = &jazz_stage_art[(stage < JAZZ_STAGE_COUNT) ? stage : 0];
    jj1ActiveBlockTiles = stageArt->blockTiles;
    jj1ActiveBlocks = stageArt->blocks;

    PAL_setPalette(PAL2, stageArt->blockPalette, DMA);
    PAL_setPalette(PAL1, stageArt->pal1, DMA);
    VDP_loadTileData(stageArt->springTiles, T_SPRING,
                     JJ1_SPRING_VARIANTS * JJ1_SPRING_TILES_PER_VARIANT, DMA);
    /* Force the streamed slots to refill against the new art. */
    playerSlotFrame = 0xFF;
    for (u8 i = 0; i < JAZZ_MAX_ENEMIES; i++) enemySlotArt[i] = 0xFFFF;

    render_jj1_sky();
    reset_jj1_cache();
    render_jj1_map(0, 0);
    VDP_setHorizontalScroll(BG_A, 0);
    VDP_setHorizontalScroll(BG_B, 0);
}

static void load_video_assets(void)
{
    VDP_setScreenWidth320();
    VDP_setPlaneSize(64, 32, TRUE);
    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_loadTileData(jazz_world_tiles, T_EMPTY, 7, DMA);
    /* Jazz streams like the enemies: only the current frame is resident. */
    VDP_loadTileData(jazz_enemy_walker_tiles, T_ENEMY_WALK, 2 * JJ1_WALKER_FRAME_TILES, DMA);
    VDP_loadTileData(jazz_enemy_flyer_tiles, T_ENEMY_FLY, 2 * JJ1_FLYER_FRAME_TILES, DMA);
    VDP_loadTileData(jazz_enemy_hazard_tiles, T_ENEMY_HAZARD, 4, DMA);
    VDP_loadTileData(jazz_gem_tile, T_GEM, 1, DMA);
    VDP_loadTileData(jazz_bullet_tile, T_BULLET, 1, DMA);
    VDP_loadTileData(jj1_sky_tiles, T_JJ1_SKY, 8, DMA);
    PAL_setPalette(PAL0, jazz_pal0, DMA);
    PAL_setPalette(PAL3, jj1_sky_palette, DMA);
    VDP_setBackgroundColor(0);
    VDP_setTextPalette(PAL0);
    VDP_setTextPriority(TRUE);
    PSG_reset();
}

/* Follow the player with lag proportional to the distance, as the original
 * does (it lerps the viewport toward the player by a fraction each tick).
 * The previous code moved the camera by at most 3 px per frame, but Jazz runs
 * at over 5 px per frame: the camera could never keep up, so it fell behind
 * and then lurched forward, which read as jittery back-and-forth scrolling.
 * Vertically it was worse - a 45-degree mask ramp moves the player 4-6 px per
 * frame, so the capped camera climbed in visible chunks and made smooth slopes
 * look like stairs.  A proportional step has no speed ceiling: at a constant
 * player speed it settles into a constant lag and then tracks exactly. */
static s16 camera_follow(s16 camera, s16 wanted)
{
    s16 delta = wanted - camera;
    s16 step;
    if (!delta) return camera;
    step = delta >> 2;
    if (!step) step = (delta > 0) ? 1 : -1;  /* always converge the last few px */
    return (s16)(camera + step);
}

static void update_camera_and_map(void)
{
    s16 wantedX = game.player.x - 150;
    s16 wantedY = game.player.y - 96;
    if (wantedX < 0) wantedX = 0;
    if (wantedY < 0) wantedY = 0;
    if (wantedX > ((256 * 32) - SCREEN_W)) wantedX = (256 * 32) - SCREEN_W;
    if (wantedY > ((64 * 32) - 224)) wantedY = (64 * 32) - 224;
    cameraX = camera_follow(cameraX, wantedX);
    cameraY = camera_follow(cameraY, wantedY);
    render_jj1_map(cameraX >> 5, cameraY >> 5);
    VDP_setHorizontalScroll(BG_A, -cameraX);
    VDP_setVerticalScroll(BG_A, cameraY);
    VDP_setHorizontalScroll(BG_B, 0);
}

static void put_sprite(u16 *count, s16 x, s16 y, u8 size, u16 attr)
{
    if ((*count < 79) && (x > -40) && (x < 336) && (y > -40) && (y < 240)) {
        VDP_setSpriteFull(*count, x, y, size, attr, (u8)(*count + 1));
        (*count)++;
    }
}

/* Springs and uncollected items are drawn straight from the original event
 * grid within the visible 11x8 block window; the taken bitmap hides
 * collected cells. */
static void render_jj1_events(u16 *count)
{
    s16 sx, sy;
    for (sy = jj1MapOriginY; sy < jj1MapOriginY + 8; sy++) {
        for (sx = jj1MapOriginX; sx < jj1MapOriginX + 11; sx++) {
            const Jj1EventInfo *info =
                jj1_event_info(game.stage, jj1_runtime_event(game.stage, sx, sy));
            if (info->klass == JJ1_CLASS_SPRING) {
                /* param carries the launch magnitude; bucket it into the
                   three visual strengths. */
                u16 variant = (info->param >= 12) ? 2 : ((info->param >= 9) ? 1 : 0);
                put_sprite(count, (sx << 5) - cameraX, (sy << 5) - cameraY + 16,
                    SPRITE_SIZE(4, 2), TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE,
                    T_SPRING + (variant * JJ1_SPRING_TILES_PER_VARIANT)));
            } else if ((info->klass == JJ1_CLASS_ITEM) &&
                       !jazz_event_taken(&game, (u8)sx, (u8)sy)) {
                put_sprite(count, (sx << 5) - cameraX + 12, (sy << 5) - cameraY + 12,
                    SPRITE_SIZE(1, 1), TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, T_GEM));
            }
        }
    }
}

static void render_sprites(void)
{
    u16 count = 0;
    u8 i;
    VDP_resetSprites();
    {
        u8 frame = (u8)((game.frame >> 3) & (JJ1_PLAYER_FRAME_COUNT - 1));
        if (playerSlotFrame != frame) {
            VDP_loadTileData(stageArt->playerTiles + ((u32)frame * JJ1_PLAYER_FRAME_TILES * 8),
                             T_PLAYER, JJ1_PLAYER_FRAME_TILES, DMA);
            playerSlotFrame = frame;
        }
        if (!(game.invulnerability && (game.frame & 4)))
            /* The 32px MAINCHAR frames have their visible feet on the bottom
               row, so align the frame bottom with the 22px collision body's
               floor: draw at y + PLAYER_H - 32 = y - 10. */
            put_sprite(&count, game.player.x - cameraX - 8, game.player.y - cameraY - 10,
                       SPRITE_SIZE(4, 4),
                       TILE_ATTR_FULL(PAL1, TRUE, FALSE, !game.player.facing, T_PLAYER));
    }
    render_jj1_events(&count);
    for (i = 0; i < JAZZ_MAX_ENEMIES; i++)
        if (game.enemies[i].active) {
            const JazzEnemy *e = &game.enemies[i];
            u8 flip = (e->direction < 0);
            /* Runtime check, not #ifdef: the generated include is compiled
               into gfx.c, so this translation unit never sees its macro.  gfx.c
               exports a null pointer when no original sprites were extracted. */
            u8 id = jj1_runtime_event(game.stage, e->gridX, e->gridY);
            const Jj1EventSprite *art = stageArt->sprites ? &stageArt->sprites[id] : 0;
            if (art && art->frames) {
                /* One VRAM slot per active enemy: upload the current frame only
                   when it changes, so a walking enemy costs one DMA every few
                   frames rather than one every frame. */
                u16 slot = T_ENEMY_BASE + (i * JJ1_ENEMY_SLOT_TILES);
                u8 frame = (u8)((game.frame >> 3) % art->frames);
                u16 wanted = (u16)((id << 3) | frame);
                u16 cellW = (u16)(art->tilesW << 3);
                u16 cellH = (u16)(art->tilesH << 3);
                u16 tile = slot;
                u8 cx, cy;
                if (enemySlotArt[i] != wanted) {
                    VDP_loadTileData(stageArt->spriteTiles +
                                     ((u32)(art->tile + (frame * art->tilesW * art->tilesH)) * 8),
                                     slot, art->tilesW * art->tilesH, DMA);
                    enemySlotArt[i] = wanted;
                }
                /* The cell is centred on the body and stands on its feet; it is
                   drawn as up to 2x2 chunks because one hardware sprite can
                   only cover 4x4 tiles. */
                for (cy = 0; cy < art->tilesH; cy += 4) {
                    u8 ch = (u8)((art->tilesH - cy > 4) ? 4 : (art->tilesH - cy));
                    for (cx = 0; cx < art->tilesW; cx += 4) {
                        u8 cw = (u8)((art->tilesW - cx > 4) ? 4 : (art->tilesW - cx));
                        s16 ox = (s16)(cx << 3);
                        /* A flipped sprite mirrors the chunk layout too. */
                        if (flip) ox = (s16)(cellW - ox - (cw << 3));
                        put_sprite(&count,
                                   e->x - cameraX + (JAZZ_ENEMY_W >> 1) - (s16)(cellW >> 1) + ox,
                                   e->y - cameraY + JAZZ_ENEMY_H - (s16)cellH + (s16)(cy << 3),
                                   SPRITE_SIZE(cw, ch),
                                   TILE_ATTR_FULL(PAL1, TRUE, FALSE, flip, tile));
                        tile = (u16)(tile + (cw * ch));
                    }
                }
                continue;
            }
            if (e->klass == JJ1_CLASS_ENEMY_WALK)
                put_sprite(&count, e->x - cameraX - 9, e->y - cameraY,
                    SPRITE_SIZE(4, 2), TILE_ATTR_FULL(PAL1, TRUE, FALSE, flip,
                    T_ENEMY_WALK + (((game.frame >> 3) & 1) * JJ1_WALKER_FRAME_TILES)));
            else if (e->klass == JJ1_CLASS_ENEMY_FLY)
                put_sprite(&count, e->x - cameraX - 1, e->y - cameraY,
                    SPRITE_SIZE(2, 2), TILE_ATTR_FULL(PAL1, TRUE, FALSE, flip,
                    T_ENEMY_FLY + (((game.frame >> 3) & 1) * JJ1_FLYER_FRAME_TILES)));
            else
                put_sprite(&count, e->x - cameraX - 1, e->y - cameraY,
                    SPRITE_SIZE(2, 2), TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE,
                    T_ENEMY_HAZARD));
        }
    for (i = 0; i < JAZZ_MAX_BULLETS; i++)
        if (game.bullets[i].active)
            put_sprite(&count, game.bullets[i].x - cameraX, game.bullets[i].y - cameraY, SPRITE_SIZE(1, 1), TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, T_BULLET));
    if (count) {
        VDP_setSpriteLink(count - 1, 0);
        VDP_updateSprites(count, DMA);
    } else VDP_clearSprites();
}

static void update_sfx(void)
{
    /* Original JJ1 SOUNDS.000 clips, locally converted to signed PCM by the
       importer. PCM is used for recognisable jump, gun, and pickup effects;
       PSG remains a compact fallback for the hurt chirp. */
    if (game.events & JAZZ_EVENT_FIRE)
        SND_PCM_startPlay(jj1_fire, sizeof(jj1_fire), SOUND_PCM_RATE_11025, SOUND_PAN_CENTER, FALSE);
    else if (game.events & JAZZ_EVENT_JUMP)
        SND_PCM_startPlay(jj1_jump, sizeof(jj1_jump), SOUND_PCM_RATE_11025, SOUND_PAN_CENTER, FALSE);
    else if (game.events & JAZZ_EVENT_GEM)
        SND_PCM_startPlay(jj1_pickup, sizeof(jj1_pickup), SOUND_PCM_RATE_11025, SOUND_PAN_CENTER, FALSE);

    if (sfxTimer) {
        sfxTimer--;
        if (!sfxTimer) PSG_setEnvelope(0, PSG_ENVELOPE_MIN);
    }
    if (game.events & JAZZ_EVENT_HURT) { PSG_setFrequency(0, 500); PSG_setEnvelope(0, 2); sfxTimer = 7; }
}

static void begin_play(void)
{
    jazz_game_init(&game);
    /* Level select: start on the stage chosen on the title screen. */
    if (selectedStage) jazz_debug_set_stage(&game, selectedStage);
    game.previousInput = JAZZ_INPUT_START;
    VDP_setWindowOnTop(2);
    VDP_clearPlane(WINDOW, TRUE);
    hud_invalidate();
    render_backdrop(game.stage);
    render_level_map();
    renderedStage = game.stage;
    renderedDestroyCount = game.destroyCount;
    for (u8 s = 0; s < JAZZ_MAX_ENEMIES; s++) enemySlotArt[s] = 0xFFFF;
    playerSlotFrame = 0xFF;
    mode = MODE_PLAY;
}

/* Level select: UP/DOWN on the title screen pick the starting stage, which
 * makes it possible to debug a later level without playing through to it. */
static void draw_level_select(void)
{
    char line[24];
    /*                 0123456789..           digit sits at index 11 */
    const char *label = "LEVEL:  <  1  >   ";
    u8 i;
    for (i = 0; label[i]; i++) line[i] = label[i];
    line[11] = (char)('1' + selectedStage);
    line[i] = 0;
    VDP_drawTextBG(WINDOW, line, 11, 16);
    VDP_drawTextBG(WINDOW, "UP/DOWN: SELECT LEVEL", 9, 19);
}

static void show_title(void)
{
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    VDP_setWindowFullScreen();
    VDP_clearPlane(WINDOW, TRUE);
    VDP_setTextPlane(WINDOW);
    VDP_drawTextBG(WINDOW, "JAZZ JACKRABBIT", 11, 7);
    VDP_drawTextBG(WINDOW, "GENESIS HOME PORT", 10, 9);
    VDP_drawTextBG(WINDOW, "SGDK PLAYABLE PROTOTYPE", 7, 13);
    VDP_drawTextBG(WINDOW, "START: PLAY", 13, 18);
    VDP_drawTextBG(WINDOW, "D-PAD RUN    A FIRE", 10, 21);
    VDP_drawTextBG(WINDOW, "B OR C JUMP  START PAUSE", 6, 23);
    VDP_drawTextBG(WINDOW, "JJ1 SHAREWARE ASSET BUILD", 7, 26);
    draw_level_select();
    VDP_clearSprites();
    mode = MODE_TITLE;
}

static void show_end(const char *title, const char *message)
{
    VDP_setWindowFullScreen();
    VDP_clearPlane(WINDOW, TRUE);
    VDP_setTextPlane(WINDOW);
    VDP_drawTextBG(WINDOW, title, 12, 10);
    VDP_drawTextBG(WINDOW, message, 7, 14);
    VDP_drawTextBG(WINDOW, "PRESS START FOR TITLE", 9, 20);
}

static void run_play_frame(u16 input)
{
    jazz_step(&game, input);
    if (game.stage != renderedStage) {
        render_backdrop(game.stage);
        render_level_map();
        renderedStage = game.stage;
    }
    update_camera_and_map();
    if (game.destroyCount != renderedDestroyCount) {
        draw_jj1_block(game.destroyedX, game.destroyedY);
        renderedDestroyCount = game.destroyCount;
    }
    draw_status_line(&game);
    render_sprites();
    update_sfx();
    if (game.finished) {
        mode = (game.lives ? MODE_COMPLETE : MODE_GAMEOVER);
        show_end((mode == MODE_COMPLETE) ? "EPISODE COMPLETE" : "GAME OVER", (mode == MODE_COMPLETE) ? "CARROT KINGDOM IS SAFE" : "TRY AGAIN, JAZZ!");
    }
}

#ifdef JAZZ_AUTOTEST
static u8 run_target_point_to_point_tests(void)
{
    JazzGame *probe = &game;
    s16 startX;

    /* This executes in the actual 68000 binary. Keep it bounded and use the
       global game state: the Genesis user stack is intentionally only 512 B. */
    jazz_game_init(probe);
    if ((probe->lives != 3) || (probe->health != 5)) return 0;
    /* The spawn settle must have found real mask ground under Jazz. */
    if (!jj1_runtime_down_point_solid(probe->stage, probe->player.x + 7,
                                      probe->player.y + 22)) return 0;
    /* The original level must contain items and an end-of-level event. */
    if (!probe->stageGemTotal) return 0;
    startX = probe->player.x;
    { u8 i; for (i = 0; i < 4; i++) jazz_step(probe, JAZZ_INPUT_RIGHT); }
    if (probe->player.x <= startX) return 0;
    return 1;
}
#endif

int main(bool hardReset)
{
    u16 pad;
    (void)hardReset;
#ifdef JAZZ_AUTOTEST
    /* Boot marker: distinct from the final assertion marker below. */
    SRAM_enable();
    SRAM_writeByte(0, 'B'); SRAM_writeByte(1, 'O');
    SRAM_writeByte(2, 'O'); SRAM_writeByte(3, 'T');
    SRAM_writeByte(4, 0); SRAM_disable();
#endif
    load_video_assets();
#ifdef JAZZ_AUTOTEST
    if (run_target_point_to_point_tests()) {
        /* A deterministic SRAM marker lets the external emulator runner
         * assert the result without scraping a window or debugger console. */
        SRAM_enable();
        SRAM_writeByte(0, 'J'); SRAM_writeByte(1, 'A');
        SRAM_writeByte(2, 'Z'); SRAM_writeByte(3, 'P');
        SRAM_writeByte(4, 1); SRAM_disable();
        KLog("JAZZ_P2P_PASS");
    } else {
        SRAM_enable();
        SRAM_writeByte(0, 'J'); SRAM_writeByte(1, 'A');
        SRAM_writeByte(2, 'Z'); SRAM_writeByte(3, 'P');
        SRAM_writeByte(4, 0); SRAM_disable();
        KLog("JAZZ_P2P_FAIL");
    }
    while (TRUE) SYS_doVBlankProcess();
#else
    JOY_init();
    show_title();
    while (TRUE) {
        pad = JOY_readJoypad(JOY_1);
        if (mode == MODE_TITLE) {
            u16 pressed = (u16)(pad & ~titlePreviousPad);
            if ((pressed & BUTTON_UP) && (selectedStage + 1 < JAZZ_STAGE_COUNT)) {
                selectedStage++;
                draw_level_select();
            } else if ((pressed & BUTTON_DOWN) && selectedStage) {
                selectedStage--;
                draw_level_select();
            }
            titlePreviousPad = pad;
            if (pad & BUTTON_START) begin_play();
        } else if (mode == MODE_PLAY) {
            run_play_frame(input_from_pad(pad));
        } else if (pad & BUTTON_START) {
            show_title();
        }
        SYS_doVBlankProcess();
    }
#endif
    return 0;
}
