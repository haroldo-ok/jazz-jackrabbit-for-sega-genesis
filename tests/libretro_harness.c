/* Headless libretro harness for Genesis point-to-point tests.
 *
 * Loads a libretro core (e.g. Genesis Plus GX), runs a ROM for a scripted
 * number of frames with scripted joypad input, dumps RGB565 frames as PPM
 * images, and prints the first bytes of cartridge SRAM so a wrapper script
 * can assert on the ROM's self-test marker.  No display or audio device is
 * needed, so the same test runs identically in CI.
 *
 * Usage:
 *   libretro_harness CORE.so ROM.bin TOTAL_FRAMES SCRIPT OUT_PREFIX
 *
 * SCRIPT is a comma-separated list of directives:
 *   <frame>-<frame>:<button>   hold button over the frame range
 *   shot@<frame>               dump the frame to OUT_PREFIX<frame>.ppm
 * Buttons: start, a, b, c, up, down, left, right.
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "libretro.h"

#define MAX_DIRECTIVES 64

typedef struct { unsigned first, last; unsigned id; } HoldDirective;
typedef struct { unsigned frame; } ShotDirective;

static HoldDirective holds[MAX_DIRECTIVES];
static ShotDirective shots[MAX_DIRECTIVES];
static ShotDirective audios[MAX_DIRECTIVES];
static unsigned holdCount, shotCount, audioCount;
static unsigned currentFrame, totalFrames;
static const char *outPrefix;

static enum retro_pixel_format pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
static uint8_t lastFrame[1024 * 576 * 4];
static unsigned lastW, lastH;

/* --- libretro callbacks --- */

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
    (void)level;
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
}

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixelFormat = *(const enum retro_pixel_format *)data;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = ".";
        return true;
    default:
        return false;
    }
}

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
    unsigned y;
    if (!data) return;
    lastW = width;
    lastH = height;
    for (y = 0; y < height; y++)
        memcpy(lastFrame + (size_t)y * width * 2,
               (const uint8_t *)data + y * pitch, (size_t)width * 2);
}

/* Audio is summarised, never recorded: the harness only accumulates signal
 * energy and a peak so a test can assert that the music driver is actually
 * producing sound (and that different stages differ), without keeping any
 * audio.  Reset between measurement windows via audio_reset(). */
static double audioEnergy;
static unsigned long audioSamples;
static int audioPeak;

static void audio_reset(void)
{
    audioEnergy = 0.0;
    audioSamples = 0;
    audioPeak = 0;
}

static void audio_accumulate(int16_t sample)
{
    int magnitude = sample < 0 ? -sample : sample;
    audioEnergy += (double)sample * (double)sample;
    audioSamples++;
    if (magnitude > audioPeak) audioPeak = magnitude;
}

static void audio_cb(int16_t left, int16_t right)
{
    audio_accumulate(left);
    audio_accumulate(right);
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
    size_t i;
    for (i = 0; i < frames * 2; i++) audio_accumulate(data[i]);
    return frames;
}
static void input_poll_cb(void) { }

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    unsigned i;
    (void)index;
    if (port != 0 || device != RETRO_DEVICE_JOYPAD) return 0;
    for (i = 0; i < holdCount; i++)
        if ((holds[i].id == id) && (currentFrame >= holds[i].first) && (currentFrame <= holds[i].last))
            return 1;
    return 0;
}

/* --- script parsing --- */

static unsigned button_id(const char *name)
{
    /* Genesis Plus GX RetroPad mapping: Y=MD A (fire), B=MD B (jump),
       A=MD C, START=start. */
    if (!strcmp(name, "a")) return RETRO_DEVICE_ID_JOYPAD_Y;
    if (!strcmp(name, "b")) return RETRO_DEVICE_ID_JOYPAD_B;
    if (!strcmp(name, "c")) return RETRO_DEVICE_ID_JOYPAD_A;
    if (!strcmp(name, "start")) return RETRO_DEVICE_ID_JOYPAD_START;
    if (!strcmp(name, "up")) return RETRO_DEVICE_ID_JOYPAD_UP;
    if (!strcmp(name, "down")) return RETRO_DEVICE_ID_JOYPAD_DOWN;
    if (!strcmp(name, "left")) return RETRO_DEVICE_ID_JOYPAD_LEFT;
    if (!strcmp(name, "right")) return RETRO_DEVICE_ID_JOYPAD_RIGHT;
    fprintf(stderr, "unknown button '%s'\n", name);
    exit(2);
}

static void parse_script(char *script)
{
    char *token = strtok(script, ",");
    while (token) {
        if (!strncmp(token, "shot@", 5)) {
            if (shotCount < MAX_DIRECTIVES) shots[shotCount++].frame = (unsigned)atoi(token + 5);
        } else if (!strncmp(token, "audio@", 6)) {
            /* Report accumulated audio energy at this frame, then reset, so a
               test can measure one window per stage. */
            if (audioCount < MAX_DIRECTIVES) audios[audioCount++].frame = (unsigned)atoi(token + 6);
        } else {
            unsigned first, last;
            char name[16];
            if (sscanf(token, "%u-%u:%15s", &first, &last, name) == 3 && holdCount < MAX_DIRECTIVES) {
                holds[holdCount].first = first;
                holds[holdCount].last = last;
                holds[holdCount].id = button_id(name);
                holdCount++;
            } else {
                fprintf(stderr, "bad directive '%s'\n", token);
                exit(2);
            }
        }
        token = strtok(NULL, ",");
    }
}

static void dump_ppm(unsigned frame)
{
    char path[512];
    unsigned x, y;
    FILE *f;
    if (!lastW || !lastH) return;
    snprintf(path, sizeof(path), "%s%u.ppm", outPrefix, frame);
    f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fprintf(f, "P6\n%u %u\n255\n", lastW, lastH);
    for (y = 0; y < lastH; y++) {
        for (x = 0; x < lastW; x++) {
            uint16_t px = ((const uint16_t *)lastFrame)[y * lastW + x];
            uint8_t rgb[3];
            if (pixelFormat == RETRO_PIXEL_FORMAT_RGB565) {
                rgb[0] = (uint8_t)(((px >> 11) & 0x1F) << 3);
                rgb[1] = (uint8_t)(((px >> 5) & 0x3F) << 2);
                rgb[2] = (uint8_t)((px & 0x1F) << 3);
            } else {
                rgb[0] = (uint8_t)(((px >> 10) & 0x1F) << 3);
                rgb[1] = (uint8_t)(((px >> 5) & 0x1F) << 3);
                rgb[2] = (uint8_t)((px & 0x1F) << 3);
            }
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    fprintf(stderr, "wrote %s (%ux%u)\n", path, lastW, lastH);
}

/* --- core loading --- */

#define SYM(handle, name) name##_fn = (name##_t)dlsym(handle, #name); \
    if (!name##_fn) { fprintf(stderr, "missing symbol %s\n", #name); return 2; }

typedef void (*retro_set_environment_t)(retro_environment_t);
typedef void (*retro_set_video_refresh_t)(retro_video_refresh_t);
typedef void (*retro_set_audio_sample_t)(retro_audio_sample_t);
typedef void (*retro_set_audio_sample_batch_t)(retro_audio_sample_batch_t);
typedef void (*retro_set_input_poll_t)(retro_input_poll_t);
typedef void (*retro_set_input_state_t)(retro_input_state_t);
typedef void (*retro_init_t)(void);
typedef bool (*retro_load_game_t)(const struct retro_game_info *);
typedef void (*retro_run_t)(void);
typedef void *(*retro_get_memory_data_t)(unsigned);
typedef size_t (*retro_get_memory_size_t)(unsigned);

int main(int argc, char **argv)
{
    void *core;
    FILE *rom;
    long romSize;
    void *romData;
    struct retro_game_info info;
    unsigned i;

    retro_set_environment_t retro_set_environment_fn;
    retro_set_video_refresh_t retro_set_video_refresh_fn;
    retro_set_audio_sample_t retro_set_audio_sample_fn;
    retro_set_audio_sample_batch_t retro_set_audio_sample_batch_fn;
    retro_set_input_poll_t retro_set_input_poll_fn;
    retro_set_input_state_t retro_set_input_state_fn;
    retro_init_t retro_init_fn;
    retro_load_game_t retro_load_game_fn;
    retro_run_t retro_run_fn;
    retro_get_memory_data_t retro_get_memory_data_fn;
    retro_get_memory_size_t retro_get_memory_size_fn;

    if (argc != 6) {
        fprintf(stderr, "usage: %s CORE.so ROM.bin FRAMES SCRIPT OUT_PREFIX\n", argv[0]);
        return 2;
    }
    totalFrames = (unsigned)atoi(argv[3]);
    parse_script(argv[4]);
    outPrefix = argv[5];

    core = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
    if (!core) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    SYM(core, retro_set_environment)
    SYM(core, retro_set_video_refresh)
    SYM(core, retro_set_audio_sample)
    SYM(core, retro_set_audio_sample_batch)
    SYM(core, retro_set_input_poll)
    SYM(core, retro_set_input_state)
    SYM(core, retro_init)
    SYM(core, retro_load_game)
    SYM(core, retro_run)
    SYM(core, retro_get_memory_data)
    SYM(core, retro_get_memory_size)

    retro_set_environment_fn(env_cb);
    retro_set_video_refresh_fn(video_cb);
    retro_set_audio_sample_fn(audio_cb);
    retro_set_audio_sample_batch_fn(audio_batch_cb);
    retro_set_input_poll_fn(input_poll_cb);
    retro_set_input_state_fn(input_state_cb);
    retro_init_fn();

    rom = fopen(argv[2], "rb");
    if (!rom) { perror(argv[2]); return 2; }
    fseek(rom, 0, SEEK_END);
    romSize = ftell(rom);
    fseek(rom, 0, SEEK_SET);
    romData = malloc((size_t)romSize);
    if (fread(romData, 1, (size_t)romSize, rom) != (size_t)romSize) { perror("fread"); return 2; }
    fclose(rom);

    memset(&info, 0, sizeof(info));
    info.path = argv[2];
    info.data = romData;
    info.size = (size_t)romSize;
    if (!retro_load_game_fn(&info)) { fprintf(stderr, "retro_load_game failed\n"); return 2; }

    audio_reset();
    for (currentFrame = 0; currentFrame < totalFrames; currentFrame++) {
        retro_run_fn();
        for (i = 0; i < shotCount; i++)
            if (shots[i].frame == currentFrame) dump_ppm(currentFrame);
        for (i = 0; i < audioCount; i++)
            if (audios[i].frame == currentFrame) {
                double rms = audioSamples ? sqrt(audioEnergy / (double)audioSamples) : 0.0;
                printf("AUDIO %u rms=%.1f peak=%d samples=%lu\n",
                       currentFrame, rms, audioPeak, audioSamples);
                audio_reset();
            }
    }

    /* Print the first SRAM bytes for marker assertions. */
    {
        uint8_t *sram = (uint8_t *)retro_get_memory_data_fn(RETRO_MEMORY_SAVE_RAM);
        size_t size = retro_get_memory_size_fn(RETRO_MEMORY_SAVE_RAM);
        printf("SRAM:");
        if (sram && size) {
            for (i = 0; i < 12 && i < size; i++) printf(" %02X", sram[i]);
        } else printf(" none");
        printf("\n");
    }
    return 0;
}
