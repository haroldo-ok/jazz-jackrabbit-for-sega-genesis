/* Original shareware level-clear / bonus-clear .0SC animations.
 * Art and frame deltas are decoded offline, then streamed to fixed tile slots.
 * The animation formats are based on OpenJazz (GPL-2.0-or-later).
 */
#include <genesis.h>
#include "jj1_options.h"
#include "jj1_scenes.h"
#include "jazz_gfx.h"
#include "jj1_scenes_res.h"
#include "jj1_episode.h"
#include "jj1_scene_types.h"
#include "jj1_pack.h"

#define SCENE_W 40
#define SCENE_H 25
#define SCENE_TILES (SCENE_W * SCENE_H)
#define SCENE_TILE_BASE TILE_USER_INDEX
#define SCENE_FRAME_BYTES (SCENE_TILES * 32UL)

/* The lite cartridge keeps only the title and the menu pictures. */
#include "jj1_title_palettes.inc"

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

/* Lite cartridge: the cutscenes become short text screens. */
static void text_screen(const char *const *lines, u8 count, u16 seconds, const u8 *music, u8 sound)
{
    const u16 refresh = SYS_isPAL() ? 50 : 60;
    u16 previous = JOY_readJoypad(JOY_1), t;
    u8 i;
    static const u16 pal[16] = { 0x000, 0xEEE, 0x0CE, 0x0AE, 0x08C, 0x06A, 0x048, 0x026,
                                 0x2E2, 0x0A0, 0x060, 0xE80, 0xA40, 0x620, 0x888, 0x444 };
    (void)sound;
    XGM_stopPlay();
    scene_setup(0, pal);
    VDP_clearPlane(BG_A, TRUE);
    VDP_setTextPlane(BG_A); VDP_setTextPalette(PAL0);
    for (i = 0; i < count; i++) {
        const char *l = lines[i];
        u16 n = 0;
        while (l[n]) n++;
        VDP_drawText(l, (u16)((40 - n) >> 1), (u16)(12 - count + i * 2));
    }
    if (music) jj1_music_start(music);
    for (t = 0; !seconds || t < seconds * refresh; t++) {
        u16 pad = JOY_readJoypad(JOY_1);
        if ((pad & ~previous) & (BUTTON_START | BUTTON_A | BUTTON_C)) break;
        previous = pad;
        SYS_doVBlankProcess();
    }
    XGM_stopPlay();
    VDP_clearPlane(BG_A, TRUE);
}

void jj1_play_info_pages(unsigned char order)
{
    static const char *const help[] = {
        "JAZZ JACKRABBIT",
        "LEFT/RIGHT  WALK, HOLD TO RUN",
        "B  JUMP     A  FIRE",
        "C  CHANGE WEAPON",
        "DOWN  CROUCH    START  PAUSE",
        "FIND THE EXIT SIGN, BEAT THE GUARDIAN",
    };
    static const char *const order_[] = {
        "THIS CARTRIDGE HOLDS EVERY EPISODE",
        "IT WAS BUILT WITH, IN 5 MB.",
        "EPISODES 2-9 AND HOLIDAY HARE COME",
        "FROM THE REGISTERED / CD GAME.",
    };
    if (order) text_screen(order_, 4, 0, 0, 0);
    else text_screen(help, 6, 0, 0, 0);
}

unsigned short jj1_play_planet(unsigned char stage, unsigned short previous)
{
    char name[24];
    const char *lines[3];
    const char *w;
    u8 index, i, n;
    if (stage >= JJ1_PACK_STAGES) return previous;
    index = JJ1_GAME_PACK->planet[stage];
    if (index == 0xFF) return previous;
    if ((u16)(index + 1 + (jj1_episode << 4)) == previous) return previous;
    /* the n-th word of the episode's world list; past the end: the guardian */
    w = jj1_pack_dir[jj1_episode].worlds;
    for (i = 0; i < index && *w; i++) {
        while (*w && *w != ' ') w++;
        while (*w == ' ') w++;
    }
    if (*w) { for (n = 0; w[n] && w[n] != ' ' && n < 23; n++) name[n] = w[n]; name[n] = 0; }
    else { const char *g = "THE GUARDIAN"; for (n = 0; g[n]; n++) name[n] = g[n]; name[n] = 0; }
    lines[0] = "NOW APPROACHING"; lines[1] = ""; lines[2] = name;
    text_screen(lines, 3, 2, 0, 0);
    return (u16)(index + 1 + (jj1_episode << 4));
}

void jj1_play_episode_ending(const unsigned char *music)
{
    const char *lines[4];
    lines[0] = "CONGRATULATIONS!";
    lines[1] = "";
    lines[2] = jj1_pack_dir[jj1_episode].title;
    lines[3] = "IS COMPLETE";
    text_screen(lines, 4, 0, music, 0);
}

void jj1_play_level_clear(const unsigned char *music)
{
    static const char *const l[] = { "LEVEL COMPLETE" };
    text_screen(l, 1, 4, music, 0);
}

void jj1_play_bonus_clear(const unsigned char *music)
{
    static const char *const l[] = { "BONUS LEVEL COMPLETE" };
    text_screen(l, 1, 4, music, 0);
}

void jj1_play_startup(void) { }


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

/* ------------------------------------------------------------------------
 * The episode-selection screen, laid out as in the original game: the list
 * on the left (selected row black on white, episodes this cartridge does
 * not carry in red), the episode's picture on the right.  The pictures and
 * text come from the game's MENU.000 and FONTMN2.0FN
 * (tools/build_episode_menu.py writes packs/menu/episode_menu.inc); without
 * them the screen is skipped.
 * ------------------------------------------------------------------------ */
#if __has_include("../packs/menu/episode_menu.inc")
#include "../packs/menu/episode_menu.inc"
#include "jj1_runtime.h"

#define EP_BASE (TILE_USER_INDEX)
#define EP_PIC  (EP_BASE + EPMENU_BASE_TILES)
#define EP_HIGH (EP_PIC + EPMENU_PIC_W * EPMENU_PIC_H)
#define EP_MISS (EP_HIGH + 40)

static s8 ep_pack_of_row(u8 row)
{
    u8 i;
    for (i = 0; i < jj1_pack_count; i++)
        if (jj1_pack_dir[i].number == row + 1) return (s8)i;
    return -1;
}

static void ep_unpack(const u8 *packed, u16 vram, u16 tiles, u8 *buf)
{
    aplib_unpack((u8 *)packed, buf);
    VDP_loadTileData((const u32 *)buf, vram, tiles, DMA);
}

/* state 0: as in the base screen, 1: highlighted, 2: not on the cartridge */
static void ep_row(u8 row, u8 state, u8 missSlot)
{
    u8 x, y;
    for (y = 0; y < 2; y++)
        for (x = 0; x < 20; x++) {
            const u16 sy = (u16)(2 + row * 2 + y), sx = (u16)(1 + x);
            u16 tile;
            if (state == 1) tile = (u16)(EP_HIGH + y * 20 + x);
            else if (state == 2) tile = (u16)(EP_MISS + missSlot * 40 + y * 20 + x);
            else tile = (u16)(EP_BASE + epmenu_base_map[sy * 40 + sx]);
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE, tile), sx, sy);
        }
}

static void ep_picture(u8 row, u8 available, u8 *buf)
{
    u16 pal[32], i, x, y;
    const u8 *attr = epmenu_pic_attr[row];
    if (row >= EPMENU_PICTURES) return;
    for (i = 0; i < 32; i++) {
        u16 c = epmenu_pic_pal[row][i];
        if (!available) {
            /* the original shows missing episodes in grey */
            u16 r = (c >> 1) & 7, g = (c >> 5) & 7, b = (c >> 9) & 7;
            u16 v = (u16)((r + g * 2 + b) >> 2);
            c = (u16)((v << 9) | (v << 5) | (v << 1));
        }
        pal[i] = c;
    }
    ep_unpack(epmenu_pic[row], EP_PIC, EPMENU_PIC_W * EPMENU_PIC_H, buf);
    PAL_setColors(32, pal, 32, CPU);   /* pal is on the stack: not queued */
    for (y = 0; y < EPMENU_PIC_H; y++)
        for (x = 0; x < EPMENU_PIC_W; x++)
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL2 + attr[y * EPMENU_PIC_W + x], FALSE, FALSE, FALSE,
                             EP_PIC + y * EPMENU_PIC_W + x), EPMENU_PIC_COL + x, EPMENU_PIC_ROW + y);
}

unsigned char jj1_play_episode_menu(void)
{
    u8 *buf;
    u8 row, missSlot[EPMENU_ROWS], misses = 0, i, result;
    u16 previous, cell;
    buf = jj1_runtime_scratch();
    scene_setup(0, epmenu_text_pal);
    VDP_setEnable(FALSE);
    VDP_clearPlane(BG_A, TRUE);
    ep_unpack(epmenu_base_tiles, EP_BASE, EPMENU_BASE_TILES, buf);
    for (cell = 0; cell < 28 * 40; cell++)
        VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE, EP_BASE + epmenu_base_map[cell]),
                         cell % 40, cell / 40);
    for (i = 0; i < EPMENU_ROWS; i++) {
        missSlot[i] = 0xFF;
        if (i < EPMENU_ROWS - 1 && ep_pack_of_row(i) < 0) {
            missSlot[i] = misses;
            ep_unpack(epmenu_strip[EPMENU_ROWS + i], EP_MISS + misses * 40, 40, buf);
            ep_row(i, 2, misses);
            misses++;
        }
    }
    row = (u8)(jj1_pack_dir[jj1_episode].number - 1);
    if (row >= EPMENU_ROWS) row = 0;
    ep_unpack(epmenu_strip[row], EP_HIGH, 40, buf);
    ep_row(row, 1, 0);
    ep_picture(row, missSlot[row] == 0xFF, buf);
    VDP_setEnable(TRUE);
    previous = JOY_readJoypad(JOY_1);
    while (TRUE) {
        u16 pad, pressed;
        SYS_doVBlankProcess();
        pad = JOY_readJoypad(JOY_1);
        pressed = pad & ~previous;
        previous = pad;
        if (pressed & (BUTTON_B | BUTTON_C)) { result = 0xFF; break; }
        if (pressed & (BUTTON_A | BUTTON_START)) {
            if (row == EPMENU_ROWS - 1) { result = 0xFE; break; }        /* bonus levels */
            if (missSlot[row] == 0xFF) { result = (u8)ep_pack_of_row(row); break; }
        }
        if (pressed & (BUTTON_UP | BUTTON_DOWN)) {
            ep_row(row, missSlot[row] == 0xFF ? 0 : 2, missSlot[row]);
            row = (u8)((pressed & BUTTON_UP) ? (row + EPMENU_ROWS - 1) % EPMENU_ROWS : (row + 1) % EPMENU_ROWS);
            ep_unpack(epmenu_strip[row], EP_HIGH, 40, buf);
            ep_row(row, 1, 0);
            ep_picture(row, missSlot[row] == 0xFF, buf);
        }
    }
    VDP_setEnable(FALSE);
    VDP_clearPlane(BG_A, TRUE);
    return result;
}
#else
unsigned char jj1_play_episode_menu(void) { return jj1_episode; }
#endif
