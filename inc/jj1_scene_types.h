#ifndef JJ1_SCENE_TYPES_H
#define JJ1_SCENE_TYPES_H
/* Scene data records shared by the scene player (src/jj1_scenes.c) and the
 * episode packs (pack/aux_pack.c), which carry the planet approach screens
 * and the episode ending. */
#include <types.h>

typedef struct { u16 start, count; } SceneRun;
typedef struct { const SceneRun *runs; u16 count; u32 forward, backward; } SceneDelta;
typedef struct { const SceneRun *runs; u16 count; u32 offset; u8 sound; } SparseDelta;
typedef struct { const u8 *frames; const u16 *palette; const SceneDelta *deltas; u8 frameCount; u8 reverse; } SceneData;
typedef struct { u16 id; const u8 *frames; const u16 *palette; const SparseDelta *deltas; } PlanetScene;
typedef struct { const u8 *compressed[4]; const u16 *palette; } MenuPage;

/* One page of an episode ending: a 320x200 picture (40x25 tiles at `frames`)
 * and, for an animation, the tile changes of each following frame. */
typedef struct {
    const u8 *frames;
    const u16 *palette;
    const SparseDelta *deltas;
    u16 deltaCount;     /* 0 = still picture */
    u8 fps;             /* animation rate */
    u8 seconds;         /* 0 = wait for a button (stills) / run once (animations) */
} EndingPage;
#endif
