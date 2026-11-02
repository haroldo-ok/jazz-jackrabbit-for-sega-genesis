/* Original shareware level-clear / bonus-clear .0SC animations.
 * Art and frame deltas are decoded offline, then streamed to fixed tile slots.
 * The animation formats are based on OpenJazz (GPL-2.0-or-later).
 */
#include <genesis.h>
#include "jj1_options.h"
#include "jj1_scenes.h"
#include "jazz_gfx.h"
#include "jj1_scenes_res.h"
#include "jj1_menu_pages.h"
#include "jj1_episode.h"

#define SCENE_W 40
#define SCENE_H 25
#define SCENE_TILES (SCENE_W * SCENE_H)
#define SCENE_TILE_BASE TILE_USER_INDEX
#define SCENE_FRAME_BYTES (SCENE_TILES * 32UL)

typedef struct { u16 start, count; } SceneRun;
typedef struct { const SceneRun *runs; u16 count; u32 forward, backward; } SceneDelta;
typedef struct { const SceneRun *runs; u16 count; u32 offset; u8 sound; } SparseDelta;
typedef struct { const u8 *frames; const u16 *palette; const SceneDelta *deltas; u8 frameCount; u8 reverse; } SceneData;
typedef struct { u16 id; const u8 *frames; const u16 *palette; const SparseDelta *deltas; } PlanetScene;
typedef struct { const u8 *compressed[4]; const u16 *palette; } MenuPage;
#include "jj1_menu_pages.inc"
#include "jj1_scene_data.inc"
#include "jj1_planet_data.inc"
#include "jj1_ending_data.inc"
static const PlanetScene *const stage_planets[JJ1_EPISODE_STAGES] = {
#include "jj1_episode_planets.inc"
};

typedef char scene_tiles_fit[((SCENE_TILE_BASE + SCENE_TILES) <= JJ1_VRAM_TILE_LIMIT) ? 1 : -1];

static void scene_setup(const u8 *frame, const u16 *palette)
{
    u16 x, y;
    VDP_setEnable(FALSE);
    VDP_clearSprites(); VDP_updateSprites(1, DMA);
    VDP_setScreenWidth320();
    VDP_setPlaneSize(64, 32, TRUE);
    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_setHorizontalScroll(BG_A, 0); VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_A, 0); VDP_setVerticalScroll(BG_B, 0);
    VDP_clearPlane(WINDOW, TRUE); VDP_setWindowOnTop(0);
    VDP_clearPlane(BG_A, TRUE); VDP_clearPlane(BG_B, TRUE);
    PAL_setColor(0, 0);
    VDP_setBackgroundColor(0);
    PAL_setPalette(PAL1, palette, CPU);
    if (frame) VDP_loadTileData((const u32 *)frame, SCENE_TILE_BASE, SCENE_TILES, DMA);
    for (y = 0; y < SCENE_H; y++)
        for (x = 0; x < SCENE_W; x++)
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE,
                              SCENE_TILE_BASE + y * SCENE_W + x), x, y + 1);
    VDP_setEnable(TRUE);
}

static void scene_play(const SceneData *s, const u8 *music)
{
    u16 frames = 0;
    const u16 refresh = SYS_isPAL() ? 50 : 60;
    u16 previous = JOY_readJoypad(JOY_1);
    u8 current = 0, forward = 1, tick = 0;
    const u8 *data = s->frames;
    XGM_stopPlay();
    scene_setup(data, s->palette);
    if (music) jj1_music_start(music);

    /* The .0SC scripts specify six seconds at 14 frames per second. RR in
     * both files requests a reverse animation: play the same decoded frames
     * backwards, avoiding duplicated turn-around frames. */
    while (frames < (u16)(refresh * 6)) {
        u16 pad = JOY_readJoypad(JOY_1);
        if ((pad & ~previous) & BUTTON_START) break;
        previous = pad;
        tick += 14;
        if (tick >= refresh) {
            u8 next;
            const SceneDelta *delta;
            u16 i;
            tick -= refresh;
            if (forward && current + 1 >= s->frameCount) forward = 0;
            else if (!forward && current == 0) forward = 1;
            next = forward ? (u8)(current + 1) : (u8)(current - 1);
            delta = &s->deltas[forward ? current : next];
            data = s->frames + (forward ? delta->forward : delta->backward);
            for (i = 0; i < delta->count; i++) {
                const SceneRun *r = &delta->runs[i];
                VDP_loadTileData((const u32 *)data, SCENE_TILE_BASE + r->start, r->count, DMA);
                data += (u32)r->count * 32;
            }
            current = next;
        }
        SYS_doVBlankProcess();
        frames++;
    }
    XGM_stopPlay();
}


/* INSTRUCT.0SC and ORDER.0SC pages are pre-composited from their original
 * scripts. The viewer decompresses one page at a time to preserve RAM and ROM. */
void jj1_play_info_pages(unsigned char order)
{
    const MenuPage *pages = order ? order_pages : instruct_pages;
    const u8 count = order ? (sizeof(order_pages) / sizeof(order_pages[0])) :
                             (sizeof(instruct_pages) / sizeof(instruct_pages[0]));
    u16 previous = JOY_readJoypad(JOY_1);
    u8 index = 0, redraw = 1;
    u8 *infoPageBuffer;
    /* Pages are four independently compressed 250-tile strips. */
    infoPageBuffer = MEM_alloc(8000);
    if (!infoPageBuffer) return;
    while (TRUE) {
        u16 pad, pressed;
        if (redraw) {
            u8 part;
            scene_setup(0, pages[index].palette);
            VDP_setEnable(FALSE);
            for (part = 0; part < 4; part++) {
                lz4w_unpack(pages[index].compressed[part], infoPageBuffer);
                VDP_loadTileData((const u32 *)infoPageBuffer, SCENE_TILE_BASE + part * 250, 250, DMA);
            }
            VDP_setEnable(TRUE);
            /* Keep the original DOS footer but supply actual pad controls. */
            VDP_setWindowOnBottom(1);
            VDP_setTextPlane(WINDOW); VDP_setTextPalette(PAL0);
            VDP_drawTextBG(WINDOW, "B/C BACK  A/START/RIGHT NEXT", 5, 27);
            redraw = 0;
        }
        SYS_doVBlankProcess();
        pad = JOY_readJoypad(JOY_1);
        pressed = pad & ~previous;
        previous = pad;
        if (pressed & (BUTTON_B | BUTTON_C)) break;
        if (pressed & (BUTTON_RIGHT | BUTTON_DOWN | BUTTON_START | BUTTON_A)) {
            if (index + 1 >= count) break;
            index++; redraw = 1;
        } else if ((pressed & (BUTTON_LEFT | BUTTON_UP)) && index) {
            index--; redraw = 1;
        }
    }
    MEM_free(infoPageBuffer);
}

/* The original waits two seconds at double size after the approach, then
 * zooms in rapidly for a quarter second. Frames are sparse ROM tile deltas;
 * no framebuffer or temporary 32 KB RAM allocation is required. */
unsigned short jj1_play_planet(unsigned char stage, unsigned short previous)
{
    const PlanetScene *planet;
    u16 tick, previousPad;
    const u16 refresh = SYS_isPAL() ? 50 : 60;
    u8 current = 0;
    if (stage >= JJ1_EPISODE_STAGES) return previous;
    planet = stage_planets[stage];
    if (!planet || planet->id == previous) return previous;
    XGM_stopPlay();
    scene_setup(planet->frames, planet->palette);
    previousPad = JOY_readJoypad(JOY_1);
    for (tick = 0; tick < (u16)(refresh * 4 + refresh / 4); tick++) {
        u16 pad = JOY_readJoypad(JOY_1);
        u8 target;
        if ((pad & ~previousPad) & (BUTTON_START | BUTTON_B)) break;
        previousPad = pad;
        if (tick < refresh * 2) target = (u8)((tick * 10) / refresh);
        else if (tick < refresh * 4) target = 19;
        else {
            target = (u8)(20 + ((tick - refresh * 4) * 16) / refresh);
            if (target > 23) target = 23;
        }
        while (current < target) {
            const SparseDelta *d = &planet->deltas[current];
            const u8 *pixels = planet->frames + d->offset;
            u16 i;
            for (i = 0; i < d->count; i++) {
                const SceneRun *run = &d->runs[i];
                VDP_loadTileData((const u32 *)pixels, SCENE_TILE_BASE + run->start,
                                 run->count, DMA);
                pixels += (u32)run->count * 32;
            }
            current++;
        }
        SYS_doVBlankProcess();
    }
    return planet->id;
}

/* ENDE1's first, second and fourth pages wait for the player; the third
 * runs its original animation once at 14 fps. START/A advances each page. */
static void ending_wait(void)
{
    u16 previous = JOY_readJoypad(JOY_1);
    while (TRUE) {
        u16 pad = JOY_readJoypad(JOY_1);
        if ((pad & ~previous) & (BUTTON_START | BUTTON_A | BUTTON_C | BUTTON_RIGHT)) return;
        previous = pad;
        SYS_doVBlankProcess();
    }
}

void jj1_play_episode_ending(const unsigned char *music)
{
    u16 previous;
    u8 frame = 0, tick = 0;
    const u16 refresh = SYS_isPAL() ? 50 : 60;
    XGM_stopPlay();
    scene_setup(jj1_scene_ending_page_0, ending_page_0_pal);
    if (music) jj1_music_start(music);
    ending_wait();
    scene_setup(jj1_scene_ending_page_1, ending_page_1_pal);
    ending_wait();
    scene_setup(jj1_scene_ending_animation, ending_animation_pal);
    previous = JOY_readJoypad(JOY_1);
    while (frame < (sizeof(ending_animation_deltas) / sizeof(ending_animation_deltas[0]))) {
        u16 pad = JOY_readJoypad(JOY_1);
        if ((pad & ~previous) & (BUTTON_START | BUTTON_A | BUTTON_C | BUTTON_RIGHT)) break;
        previous = pad;
        tick += 14;
        if (tick >= refresh) {
            const SparseDelta *d = &ending_animation_deltas[frame++];
            const u8 *pixels = jj1_scene_ending_animation + d->offset;
            u16 i;
            tick -= refresh;
            for (i = 0; i < d->count; i++) {
                const SceneRun *run = &d->runs[i];
                VDP_loadTileData((const u32 *)pixels, SCENE_TILE_BASE + run->start, run->count, DMA);
                pixels += (u32)run->count * 32;
            }
        }
        SYS_doVBlankProcess();
    }
    scene_setup(jj1_scene_ending_page_3, ending_page_3_pal);
    ending_wait();
    XGM_stopPlay();
}

void jj1_play_level_clear(const unsigned char *music) { scene_play(&level_clear_scene, music); }
void jj1_play_bonus_clear(const unsigned char *music) { scene_play(&bonus_clear_scene, music); }

/* Sparse startup data stores only the tiles changed by each original frame. */
void jj1_play_startup(void)
{
    u16 previous = JOY_readJoypad(JOY_1), elapsed = 0;
    const u16 refresh = SYS_isPAL() ? 50 : 60;
    u8 frame = 0, tick = 0, page;
    XGM_stopPlay();
    scene_setup(jj1_scene_startup, startup_pal);
    while (frame < sizeof(startup_deltas) / sizeof(startup_deltas[0])) {
        u16 pad = JOY_readJoypad(JOY_1);
        const SparseDelta *d;
        const u8 *pixels;
        u16 i;
        if ((pad & ~previous) & BUTTON_START) return;
        previous = pad;
        tick += 14;
        if (tick >= refresh) {
            tick -= refresh;
            d = &startup_deltas[frame++];
            pixels = jj1_scene_startup + d->offset;
            for (i = 0; i < d->count; i++) {
                const SceneRun *r = &d->runs[i];
                VDP_loadTileData((const u32 *)pixels, SCENE_TILE_BASE + r->start, r->count, DMA);
                pixels += (u32)r->count * 32;
            }
            /* The .0SC sound list is JUMPA11, BLOCK3, YEAHOO. These IDs
             * are registered by load_video_assets() before startup begins. */
            if (d->sound) {
                u8 id = d->sound == 1 ? 65 : d->sound == 2 ? 78 : 70;
                jj1_sfx_start(id, 2, SOUND_PCM_CH3);
            }
        }
        SYS_doVBlankProcess();
    }
    /* Both following pages are scripted for three seconds and use the
     * original FONTMN2 glyphs, layout, and palette. */
    for (page = 0; page < 2; page++) {
        scene_setup(jj1_scene_startup_credits + (u32)page * SCENE_FRAME_BYTES,
                    startup_credits_pal);
        for (elapsed = 0; elapsed < refresh * 3; elapsed++) {
            u16 pad = JOY_readJoypad(JOY_1);
            if ((pad & ~previous) & BUTTON_START) return;
            previous = pad;
            SYS_doVBlankProcess();
        }
    }
}

void jj1_show_original_title(void)
{
    scene_setup(jj1_scene_title, title_pal);
}

/* All six menu states come from the actual MENU.000 normal and highlight
 * images. A shared tile dictionary keeps the original art within VRAM. */
void jj1_set_menu_option(unsigned char option)
{
    const u16 *map = (const u16 *)jj1_scene_menu_maps + (u32)(option % 6) * SCENE_TILES;
    u16 x, y;
    for (y = 0; y < SCENE_H; y++)
        for (x = 0; x < SCENE_W; x++)
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE,
                SCENE_TILE_BASE + map[y * SCENE_W + x]), x, y + 1);
}

void jj1_show_original_menu(unsigned char option)
{
    VDP_setEnable(FALSE);
    VDP_clearSprites(); VDP_updateSprites(1, DMA);
    VDP_setScreenWidth320();
    VDP_setPlaneSize(64, 32, TRUE);
    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_setHorizontalScroll(BG_A, 0); VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_A, 0); VDP_setVerticalScroll(BG_B, 0);
    VDP_clearPlane(WINDOW, TRUE); VDP_setWindowOnTop(0);
    VDP_clearPlane(BG_A, TRUE); VDP_clearPlane(BG_B, TRUE);
    PAL_setColor(0, 0);
    VDP_setBackgroundColor(0);
    PAL_setPalette(PAL1, menu_pal, CPU);
    VDP_loadTileData((const u32 *)jj1_scene_menu_tiles, SCENE_TILE_BASE, JJ1_MENU_TILE_COUNT, DMA);
    jj1_set_menu_option(option);
    VDP_setEnable(TRUE);
}
