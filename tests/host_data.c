/* Host-side wrapper: compiles the converted JJ1 level data (normally built
 * into the Genesis ROM through src/gfx.c) for the point-to-point tests, so
 * the host simulation runs against the real original maps and masks. */

#include "jazz_game.h"

#ifndef RGB3_3_3_TO_VDPCOLOR
#define RGB3_3_3_TO_VDPCOLOR(r, g, b) \
    ((u16)((((b) & 7) << 10) | (((g) & 7) << 6) | (((r) & 7) << 2)))
#endif

#include "jj1_level0_data.inc"
#include "jj1_level1_data.inc"
#include "jj1_level2_data.inc"
