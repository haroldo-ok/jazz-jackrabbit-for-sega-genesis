#include "genesis.h"

__attribute__((externally_visible))
const ROMHeader rom_header = {
#if (ENABLE_BANK_SWITCH != 0)
    "SEGA SSF        ",
#elif (MODULE_MEGAWIFI != 0)
    "SEGA MEGAWIFI   ",
#else
    "SEGA MEGA DRIVE ",
#endif
    "(C)SGDK 2024    ",
    "JAZZ JACKRABBIT GENESIS PORT                   ",
    "JAZZ JACKRABBIT GENESIS PORT                   ",
    "GM 20260711-00",
    0x000,
    "JD              ",
    0x00000000,
    0x003FFFFF, /* rewritten by tools/build_packs.py rom with the real end */
    0xE0FF0000,
    0xE0FFFFFF,
    "RA",
    0xF820,
    0x00200000,
    0x0020FFFF,
    "            ",
    "JAZZ JACKRABBIT: SGDK PORT              ",
    "JU              "
};
