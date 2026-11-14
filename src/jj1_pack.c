/* Episode packs: one per episode, each at its own address in the ROM
 * (see inc/jj1_pack.h and tools/build_cartridge.py). */
#include <genesis.h>
#include "jj1_pack.h"
#include "jj1_runtime.h"

#include "lite_packdir.inc"

u8 jj1_episode;
const Jj1GamePack *jj1_game_pack;

void jj1_pack_select(u8 episode)
{
    if (episode >= jj1_pack_count) episode = 0;
    jj1_episode = episode;
    jj1_game_pack = (const Jj1GamePack *)jj1_pack_dir[episode].gameAt;
    jj1_runtime_invalidate();
}

u8 jj1_pack_stages(void) { return jj1_pack_dir[jj1_episode].stages; }
