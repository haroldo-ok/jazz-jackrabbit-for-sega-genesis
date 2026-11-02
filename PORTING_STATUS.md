# Porting status — work in progress

This is not a completed shareware port. Original game data remains subject to
its owners' rights. This project does not include the raw shareware archive.

## Added in this revision

- All four menu actions now work. Load Game presents four persistent SRAM
  slots; pause with START, press C, and select a slot to save the current level
  state. An empty or checksum-invalid slot cannot be loaded. Saves are native
  to this port, not compatible with DOS/OpenJazz saves; a battery-backed SRAM
  cartridge or an emulator configured to persist save RAM is required.
- Instructions (12 pages) and Order Info (17 pages) reproduce the supplied
  shareware scenes and original text, reduced to Genesis palettes. A/START or
  RIGHT/DOWN advances; LEFT/UP goes back; B/C exits. The DOS footer remains as
  historical artwork and a Genesis control hint appears below it. Ordering
  information is archival, not a current purchase link.
- Setup Options supports persistent music and sound-effects switches and three
  physical button layouts. It is Genesis-specific, not a full copy of the DOS
  setup. Bonus controls use the configured fire button for run and jump button
  for jump (the weapon-cycle button also jumps); menus keep fixed controls.
- Level-clear/bonus-clear frames use lossless bidirectional sparse tile deltas;
  information pages are LZ4W-compressed in four 8,000-byte chunks so playback
  fits within Genesis RAM without starving SGDK's DMA buffers.

## Previously added presentation

- Original `PLANET.000`, `.001`, `.002` and `.018` planet art, names, palettes
  and FONTMN1 lettering. The approach grows for two seconds, holds for two,
  then rapidly zooms; START or B skips it. It plays at the start of a game and
  when the actual planet ID changes, not between levels of the same planet.
  Frames use precomputed 10 Hz scaling and Genesis 15-colour palettes rather
  than the DOS renderer's continuous scaling and full palette.
- The supplied `ENDE1.0SC` episode-ending background, all original scripted
  text, all 28 decoded animation frames, and the script's BONUS music choice
  (using the existing Genesis-converted bonus track). At episode victory the
  sequence returns to the original title. START, A, C or RIGHT advances pages;
  the animation plays once at 14 fps. Palette fades are not reproduced.
- Bonus outcomes use the original two-second white-out transition. A win then
  plays the existing `BONUS.0SC` animation; a loss returns without a separate
  loss movie. OpenJazz's reference logic has no separate bonus-loss .0SC scene.
  The visual effect is sampled at the bonus renderer's lower frame rate.
- Level-clear scenes now play only on the shareware levels whose level metadata
  specifies `ENDLEVEL.0SC`, rather than after every level.

## Previously converted

- All 42 decoded startup animation images from `STARTUP.0SC`, including five
  sound tags; both text pages use the original FONTMN2 glyphs and layout. START
  skips the intro. The final title and six-state main menu use original art.
- Stage-select sequence on title and main menu: UP, UP, DOWN, DOWN, LEFT,
  RIGHT, LEFT, RIGHT, B, A. UP/DOWN selects on title; LEFT/RIGHT selects on menu.
- Decoded level-clear and bonus-win scenes with original imagery and Genesis
  palette adaptations. Original BONUS.000 player sprites and BONUSMAP animation.
- Corrected original firing clips and rates for the four shareware weapon
  types, including the toaster's FLAMER instead of the Rapid Fire voice.

## Still missing or incomplete

- The secret stage `LEVEL2.000` and its secret-entry branch are not in the
  seven-stage runtime chain. Not all shareware mechanics and effects have been
  converted or validated; gameplay is not yet equivalent to the original.
- The game-over screen is still a simple placeholder.
- The converted bonus/end music is an adaptation and is not a bit-identical
  conversion of the original PSM modules. Presentation palette transitions,
  and any other unsupported original scene instructions, remain incomplete.
- No real-console, PAL-hardware or full-episode manual playthrough has been
  performed. The bonus floor's rendering rate remains low on Genesis hardware.

## Validation

- Host suite with supplied shareware: passed, including all 29 information
  pages and 116 regenerated tile chunks, source-asset and
  byte-for-byte presentation regeneration, gameplay, bonus and sound mappings.
- Genesis Plus GX: four planet approaches and same-planet suppression;
  victory to original ending and title; bonus win and loss; clear-scene return;
  boot, menu, stage cheat, gameplay, bonus graphics/music and 68000 self-test.
  Firing waveforms were compared with the original clips in the emulator.
- Menu emulator tests covered page navigation/return, empty and corrupted slot
  rejection, two separate save slots, reload in a fresh emulator instance,
  persistent options, music mute, and the stage-selection cheat.
- Release ROM is 3,801,088 bytes (under the 4 MiB unbanked cartridge limit).
  Colours and audio are Genesis adaptations, not bit-identical DOS playback.
