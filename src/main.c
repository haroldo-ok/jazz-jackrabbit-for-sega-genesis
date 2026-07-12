#include <genesis.h>
#include "jazz_game.h"
#include "jazz_gfx.h"
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
static u8 sfxTimer;

static void jj1_runtime_data(u8 stage, const u8 **blocks, const u8 **masks, const u16 **startX, const u16 **startY)
{
    *blocks = jj1_level0_blocks;
    *masks = jj1_level0_masks;
    *startX = &jj1_level0_start_x;
    *startY = &jj1_level0_start_y;
    if (stage == 1) {
        *blocks = jj1_level1_blocks;
        *masks = jj1_level1_masks;
        *startX = &jj1_level1_start_x;
        *startY = &jj1_level1_start_y;
    } else if (stage >= 2) {
        *blocks = jj1_level2_blocks;
        *masks = jj1_level2_masks;
        *startX = &jj1_level2_start_x;
        *startY = &jj1_level2_start_y;
    }
}

/* Called by the platform-neutral game core when JAZZ_JJ1_RUNTIME is enabled.
 * JJ1 masks are 8x8 cells per 32x32 block. One bit therefore represents a
 * real 4x4-pixel collision cell, which is accurate enough for slopes/ramp
 * stepping without requiring per-pixel scans on a 7.67 MHz 68000. */
static u8 jj1_runtime_event(u8 stage, s16 blockX, s16 blockY)
{
    const u8 *events = jj1_level0_events;
    if ((blockX < 0) || (blockX >= 256) || (blockY < 0) || (blockY >= 64)) return 0;
    if (stage == 1) events = jj1_level1_events;
    else if (stage >= 2) events = jj1_level2_events;
    return events[(blockY * 256) + blockX];
}

static u8 jj1_runtime_mask_cell(u8 stage, s16 cellX, s16 cellY, u8 allowOneWay)
{
    const u8 *blocks;
    const u8 *masks;
    const u16 *startX;
    const u16 *startY;
    u8 block;
    if ((cellX < 0) || (cellX >= (256 * 8)) || (cellY < 0) || (cellY >= (64 * 8))) return TRUE;
    jj1_runtime_data(stage, &blocks, &masks, &startX, &startY);
    /* Event 122 is JJ1's one-way platform: only feet moving downward land. */
    if (!allowOneWay && (jj1_runtime_event(stage, cellX >> 3, cellY >> 3) == 122)) return FALSE;
    block = blocks[((cellY >> 3) * 256) + (cellX >> 3)];
    if (block >= 240) return FALSE;
    return (masks[(block << 3) + (cellY & 7)] & (1 << (cellX & 7))) ? TRUE : FALSE;
}

u8 jj1_runtime_point_solid(u8 stage, s16 x, s16 y)
{
    return jj1_runtime_mask_cell(stage, x >> 2, y >> 2, FALSE);
}

u8 jj1_runtime_down_point_solid(u8 stage, s16 x, s16 y)
{
    return jj1_runtime_mask_cell(stage, x >> 2, y >> 2, TRUE);
}

u8 jj1_runtime_rect_solid(u8 stage, s16 x, s16 y, s16 w, s16 h)
{
    s16 cx, cy;
    s16 cx0 = x >> 2;
    s16 cx1 = (x + w - 1) >> 2;
    s16 cy0 = y >> 2;
    s16 cy1 = (y + h - 1) >> 2;
    for (cy = cy0; cy <= cy1; cy++)
        for (cx = cx0; cx <= cx1; cx++)
            if (jj1_runtime_mask_cell(stage, cx, cy, FALSE)) return TRUE;
    return FALSE;
}

u8 jj1_runtime_solid(u8 stage, s16 tx, s16 ty)
{
    return jj1_runtime_rect_solid(stage, tx << 4, ty << 4, 16, 16);
}

/* Original JJ1 spring event IDs share modifier 29; their source event
 * magnitudes are -15, -20 and -30. Convert to this port's px/frame scale. */
s8 jj1_runtime_spring_velocity(u8 stage, s16 x, s16 y, s16 w, s16 h)
{
    s16 bx0 = x >> 5;
    s16 bx1 = (x + w - 1) >> 5;
    s16 by0 = y >> 5;
    s16 by1 = (y + h - 1) >> 5;
    s16 bx, by;
    for (by = by0; by <= by1; by++) {
        for (bx = bx0; bx <= bx1; bx++) {
            u8 event = jj1_runtime_event(stage, bx, by);
            if (event == 21) return -26;
            if (event == 22) return -30;
            if (event == 23) return -36;
        }
    }
    return 0;
}

/* JJ1 stores a level jump-height parameter of -5 in this shareware set.
 * Converted to this engine's pixel/frame gravity scale it is -13 px/frame,
 * matching the original reachable one-way ledges. */
s8 jj1_runtime_jump_velocity(u8 stage)
{
    (void) stage;
    return -13;
}

void jj1_runtime_place_player(JazzGame *state, u8 stage)
{
    const u8 *blocks;
    const u8 *masks;
    const u16 *startX;
    const u16 *startY;
    jj1_runtime_data(stage, &blocks, &masks, &startX, &startY);
    (void) blocks; (void) masks;
    s16 spawnY;
    state->player.x = ((s16) *startX << 5) + 8;
    spawnY = ((s16) *startY << 5) - 22;
    /* The JJ1 start record locates the spawn grid point; settle it against
       the real mask so Jazz begins on the slope/platform rather than at a
       prototype-grid height. */
    while ((spawnY < ((64 * 32) - 23)) &&
           !jj1_runtime_down_point_solid(stage, state->player.x + 2, spawnY + 22) &&
           !jj1_runtime_down_point_solid(stage, state->player.x + 7, spawnY + 22) &&
           !jj1_runtime_down_point_solid(stage, state->player.x + 11, spawnY + 22)) spawnY++;
    state->player.y = spawnY;
    state->player.vx = 0;
    state->player.vy = 0;
    state->player.onGround = TRUE;
}

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

static void draw_status_line(const JazzGame *g)
{
    char line[41];
    char number[6];
    u8 pos = 0;
    u8 i;
    const char *prefix = "LIVES ";
    const char *gems = "  GEMS ";
    const char *score = "  SCORE ";

    VDP_clearTextAreaBG(WINDOW, 0, 0, 40, 2);
    while (*prefix) line[pos++] = *prefix++;
    number_to_text(number, g->lives); for (i = 0; number[i]; i++) line[pos++] = number[i];
    while (*gems) line[pos++] = *gems++;
    number_to_text(number, g->stageGems); for (i = 0; number[i]; i++) line[pos++] = number[i];
    line[pos++] = '/'; number_to_text(number, g->stageGemTotal); for (i = 0; number[i]; i++) line[pos++] = number[i];
    while (*score) line[pos++] = *score++;
    number_to_text(number, g->score); for (i = 0; number[i]; i++) line[pos++] = number[i];
    line[pos] = 0;
    VDP_drawTextBG(WINDOW, line, 0, 0);

    if (g->paused) VDP_drawTextBG(WINDOW, "PAUSED - START RESUMES", 8, 1);
    else if (g->transitionTimer) VDP_drawTextBG(WINDOW, "EXIT FOUND!", 14, 1);
    else {
        /* Keep text as ROM literals. A 68000 traps on the unaligned long
           stores that recent GCC can otherwise use for a stack string. */
        static const char *const healthText[6] = {
            "HP:", "HP:*", "HP:**", "HP:***", "HP:****", "HP:*****"
        };
        VDP_drawTextBG(WINDOW, healthText[g->health], 0, 1);
        VDP_drawTextBG(WINDOW, "A FIRE  B/C JUMP  START PAUSE", 10, 1);
    }
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
    const u16 *palette = jj1_level0_palette;

    /* All three shareware foreground maps now use the same 88-block runtime
       cache; only ROM source pointers and palette change by stage. */
    jj1ActiveBlockTiles = jj1_level0_block_tiles;
    jj1ActiveBlocks = jj1_level0_blocks;
    if (stage == 1) {
        jj1ActiveBlockTiles = jj1_level1_block_tiles;
        jj1ActiveBlocks = jj1_level1_blocks;
        palette = jj1_level1_palette;
    } else if (stage >= 2) {
        jj1ActiveBlockTiles = jj1_level2_block_tiles;
        jj1ActiveBlocks = jj1_level2_blocks;
        palette = jj1_level2_palette;
    }

    PAL_setPalette(PAL2, palette, DMA);
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
    VDP_loadTileData(jazz_player_tiles, T_PLAYER, JJ1_PLAYER_FRAME_COUNT * JJ1_PLAYER_FRAME_TILES, DMA);
    VDP_loadTileData(jazz_spring_tiles, T_SPRING, JJ1_SPRING_VARIANTS * JJ1_SPRING_TILES_PER_VARIANT, DMA);
    VDP_loadTileData(jazz_enemy_tiles, T_ENEMY, 4, DMA);
    VDP_loadTileData(jazz_gem_tile, T_GEM, 1, DMA);
    VDP_loadTileData(jazz_bullet_tile, T_BULLET, 1, DMA);
    VDP_loadTileData(jj1_sky_tiles, T_JJ1_SKY, 8, DMA);
    PAL_setPalette(PAL0, jazz_pal0, DMA);
    PAL_setPalette(PAL1, jazz_pal1, DMA);
    PAL_setPalette(PAL3, jj1_sky_palette, DMA);
    VDP_setBackgroundColor(0);
    VDP_setTextPalette(PAL0);
    VDP_setTextPriority(TRUE);
    PSG_reset();
}

static void update_camera_and_map(void)
{
    s16 wantedX = game.player.x - 150;
    s16 wantedY = game.player.y - 96;
    if (wantedX < 0) wantedX = 0;
    if (wantedY < 0) wantedY = 0;
    if (wantedX > ((256 * 32) - SCREEN_W)) wantedX = (256 * 32) - SCREEN_W;
    if (wantedY > ((64 * 32) - 224)) wantedY = (64 * 32) - 224;
    /* Damped camera prevents a single 4px mask-cell correction from producing
       a full 32px cache-origin redraw in the opposite direction. */
    if (cameraX < wantedX) cameraX += (wantedX - cameraX > 3) ? 3 : wantedX - cameraX;
    else if (cameraX > wantedX) cameraX -= (cameraX - wantedX > 3) ? 3 : cameraX - wantedX;
    if (cameraY < wantedY) cameraY += (wantedY - cameraY > 3) ? 3 : wantedY - cameraY;
    else if (cameraY > wantedY) cameraY -= (cameraY - wantedY > 3) ? 3 : cameraY - wantedY;
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

static void render_jj1_springs(u16 *count)
{
    const u8 *events = jj1_level0_events;
    s16 sx, sy;
    if (game.stage == 1) events = jj1_level1_events;
    else if (game.stage >= 2) events = jj1_level2_events;
    for (sy = jj1MapOriginY; sy < jj1MapOriginY + 8; sy++) {
        for (sx = jj1MapOriginX; sx < jj1MapOriginX + 11; sx++) {
            u8 event = events[(sy * 256) + sx];
            if ((event == 21) || (event == 22) || (event == 23))
                put_sprite(count, (sx << 5) - cameraX, (sy << 5) - cameraY + 16,
                    SPRITE_SIZE(4, 2), TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE,
                    T_SPRING + ((event - 21) * JJ1_SPRING_TILES_PER_VARIANT)));
        }
    }
}

static void render_sprites(void)
{
    u16 count = 0;
    u8 i;
    VDP_resetSprites();
    if (!(game.invulnerability && (game.frame & 4)))
        /* JJ1 MAINCHAR frame has a taller visual bounding box than the
           prototype 14x22 collision body; anchor its feet at the mask floor. */
        put_sprite(&count, game.player.x - cameraX - 8, game.player.y - cameraY + 10, SPRITE_SIZE(4, 4),
                   TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE,
                   T_PLAYER + (((game.frame >> 3) & (JJ1_PLAYER_FRAME_COUNT - 1)) * JJ1_PLAYER_FRAME_TILES)));
    render_jj1_springs(&count);
    for (i = 0; i < JAZZ_MAX_ENEMIES; i++)
        if (game.enemies[i].active)
            put_sprite(&count, game.enemies[i].x - cameraX, game.enemies[i].y - cameraY, SPRITE_SIZE(2, 2), TILE_ATTR_FULL(PAL3, TRUE, FALSE, FALSE, T_ENEMY));
    for (i = 0; i < JAZZ_MAX_GEMS; i++)
        if (game.gems[i].active)
            put_sprite(&count, game.gems[i].x - cameraX, game.gems[i].y - cameraY, SPRITE_SIZE(1, 1), TILE_ATTR_FULL(PAL2, TRUE, FALSE, FALSE, T_GEM));
    for (i = 0; i < JAZZ_MAX_BULLETS; i++)
        if (game.bullets[i].active)
            put_sprite(&count, game.bullets[i].x - cameraX, game.bullets[i].y - cameraY, SPRITE_SIZE(1, 1), TILE_ATTR_FULL(PAL2, TRUE, FALSE, FALSE, T_BULLET));
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
    game.previousInput = JAZZ_INPUT_START;
    VDP_setWindowOnTop(2);
    VDP_clearPlane(WINDOW, TRUE);
    render_backdrop(game.stage);
    render_level_map();
    renderedStage = game.stage;
    mode = MODE_PLAY;
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
    if (!jazz_is_solid(probe, 0, 13)) return 0;
    if (jazz_tile_at(probe, 92, 11) != JAZZ_TILE_EXIT) return 0;
    startX = probe->player.x;
    jazz_step(probe, JAZZ_INPUT_RIGHT);
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
