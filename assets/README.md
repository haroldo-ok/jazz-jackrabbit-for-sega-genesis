# Original game data handling

The raw Jazz Jackrabbit install/archive is **not** included in this repository.

The current build contains compact derived outputs generated locally from the
user-supplied shareware installation:

- eight `MAINCHAR.000` / `SPRITES.000` Jazz animation frames;
- selected `BLOCKS.000` terrain patterns;
- all 240 converted blocks, full 256×64 grids, 8×8 collision masks, and
  original start coordinates from `LEVEL0.000`, `LEVEL1.000`, and `LEVEL2.000`,
  streamed through the Genesis block cache;
- `SOUNDS.000` gun, jump, and pickup PCM effects.

`tools/import_jj1_shareware.py` converts every available block/sprite/map/UI/
font/sound record to inspectable local intermediates. The `build_sgdk_*` tools
produce the VRAM-budgeted C includes consumed by the ROM. Do not add a complete
original installation or raw archive to this tree.
