#ifndef STAGE_CHEAT_H
#define STAGE_CHEAT_H

/* Platform-neutral edge-triggered matcher. Input is one button press; a
 * simultaneous/invalid press is CHEAT_OTHER, and release/no edge is NONE. */
enum { CHEAT_NONE, CHEAT_UP, CHEAT_DOWN, CHEAT_LEFT, CHEAT_RIGHT,
       CHEAT_B, CHEAT_A, CHEAT_OTHER };

/* Returns 1 on completion; preserves overlapping prefixes (e.g. U U U D). */
static int stage_cheat_step(unsigned char *progress, unsigned char key)
{
    static const unsigned char code[10] = {
        CHEAT_UP, CHEAT_UP, CHEAT_DOWN, CHEAT_DOWN, CHEAT_LEFT,
        CHEAT_RIGHT, CHEAT_LEFT, CHEAT_RIGHT, CHEAT_B, CHEAT_A
    };
    /* longest proper prefix of code that is also a suffix */
    static const unsigned char fallback[10] = {0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    if (key == CHEAT_NONE) return 0;
    while (*progress && code[*progress] != key) *progress = fallback[*progress - 1];
    if (code[*progress] == key) ++*progress;
    if (*progress == 10) { *progress = 0; return 1; }
    return 0;
}
#endif
