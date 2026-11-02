/* Battery-backed, odd-byte SRAM. The ROM occupies this address too, so
 * interrupts and the Z80 are stopped and the mapping is restored immediately.
 * Commit signature last: interrupted or corrupt writes are rejected. */
#include <genesis.h>
#include "jj1_save.h"
#include "jj1_options.h"

#define SLOT_BASE 256UL
#define SLOT_SIZE 6144UL
#define HEADER_SIZE 8UL
typedef char slots_fit[(sizeof(JazzGame) + HEADER_SIZE <= SLOT_SIZE) ? 1 : -1];

static void begin_sram(void) { SYS_disableInts(); Z80_requestBus(TRUE); SRAM_enable(); }
static void end_sram(void) { SRAM_disable(); Z80_releaseBus(); SYS_enableInts(); }
static u16 checksum_add(u16 c, u8 b) { return (u16)((c << 1 | c >> 15) ^ b); }
static u32 base_for(u8 slot) { return SLOT_BASE + (u32)slot * SLOT_SIZE; }
static u8 valid_mapped(u8 slot)
{
    u32 b = base_for(slot); u16 i, sum = 0x4A5A;
    if (SRAM_readByte(b) != 'J' || SRAM_readByte(b+1) != 'S' ||
        SRAM_readByte(b+2) != 1 || SRAM_readByte(b+3) != (sizeof(JazzGame) >> 8) ||
        SRAM_readByte(b+4) != (sizeof(JazzGame) & 255)) return 0;
    for (i = 0; i < sizeof(JazzGame); i++) sum = checksum_add(sum, SRAM_readByte(b+HEADER_SIZE+i));
    if (SRAM_readByte(b+5) != (sum >> 8) || SRAM_readByte(b+6) != (sum & 255)) return 0;
    if (SRAM_readByte(b+HEADER_SIZE+__builtin_offsetof(JazzGame, stage)) >= JAZZ_STAGE_COUNT) return 0;
    return 1;
}
u8 jj1_save_valid(u8 slot)
{
    u8 valid; if (slot >= JJ1_SAVE_SLOTS) return 0;
    begin_sram(); valid = valid_mapped(slot); end_sram(); return valid;
}
u8 jj1_save_write(u8 slot, const JazzGame *game)
{
    u16 i, sum = 0x4A5A; u32 b; const u8 *bytes = (const u8 *)game; u8 valid;
    if (slot >= JJ1_SAVE_SLOTS) return 0;
    b = base_for(slot); begin_sram();
    SRAM_writeByte(b, 0); SRAM_writeByte(b+1, 0);
    SRAM_writeByte(b+2, 1); SRAM_writeByte(b+3, sizeof(JazzGame) >> 8);
    SRAM_writeByte(b+4, sizeof(JazzGame) & 255);
    for (i = 0; i < sizeof(JazzGame); i++) { u8 c=bytes[i]; SRAM_writeByte(b+HEADER_SIZE+i, c); sum=checksum_add(sum,c); }
    SRAM_writeByte(b+5, sum >> 8); SRAM_writeByte(b+6, sum & 255);
    SRAM_writeByte(b+1, 'S'); SRAM_writeByte(b, 'J');
    valid = valid_mapped(slot); end_sram(); return valid;
}
u8 jj1_save_read(u8 slot, JazzGame *game)
{
    u16 i; u8 valid; u32 b; u8 *bytes=(u8 *)game;
    if (slot >= JJ1_SAVE_SLOTS) return 0;
    b=base_for(slot); begin_sram(); valid=valid_mapped(slot);
    if (valid) for(i=0;i<sizeof(JazzGame);i++) bytes[i]=SRAM_readByte(b+HEADER_SIZE+i);
    end_sram(); return valid;
}
/* Separate small checksummed settings record; bytes 0-15 reserved for tests. */
void jj1_options_save(void)
{
    begin_sram(); SRAM_writeByte(16,0); SRAM_writeByte(17,jj1_music_enabled);
    SRAM_writeByte(18,jj1_sfx_enabled); SRAM_writeByte(19,jj1_controls);
    SRAM_writeByte(20,(u8)(0xA5 ^ jj1_music_enabled ^ jj1_sfx_enabled ^ jj1_controls));
    SRAM_writeByte(16,'O'); end_sram();
}
void jj1_options_load(void)
{
    u8 a,b,c,d,e; begin_sram(); a=SRAM_readByte(16); b=SRAM_readByte(17);
    c=SRAM_readByte(18); d=SRAM_readByte(19); e=SRAM_readByte(20); end_sram();
    if(a=='O' && b<=1 && c<=1 && d<3 && e==(u8)(0xA5^b^c^d)) {
        jj1_music_enabled=b; jj1_sfx_enabled=c; jj1_controls=d;
    }
}
