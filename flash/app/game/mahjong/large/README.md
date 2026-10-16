# Sheets of the 1280x720 mahjong table

The tile sheets the conventional table of the browser build at 1280x720
(`mahjong_xl.rb`) draws from. They are **made from the two sheets one
directory up** (`tiles_hand.bmp`, `tiles_river.bmp`), which are the source of
truth: edit those, then write these again with

```
ruby tools/mahjong_tilegen.rb --layout-sheets flash/app/game/mahjong flash/app/game/mahjong/large
```

(run in fmruby-core). The script scales by whole numbers (nearest
neighbour), rounds the corners again at the new size and turns by quarter
turns; the pictures are the ones of the two source sheets. It first rounds
the corners of the two source sheets in place (only the corner pixels; a
sheet already round is left as it is), so an edit by hand gets the corners
back. The radius of each sheet is `CORNER_R` in the script. It overwrites
these files without asking, so do not edit them by hand.

All are 8-bit BMPs whose pixel values are RGB332 colours, transparent colour
RGB332 `0xE3`, grids without gaps. Tile numbers as everywhere in the game:
`0..8` 1m..9m, `9` 0m (red five), `10..18` 1p..9p, `19` 0p, `20..28` 1s..9s,
`29` 0s, `30..36` 1z..7z (east south west north white green red), `37` back.
In a sheet of 10 columns, tile n is at `src_x = (n % 10) * cell_w`,
`src_y = (n / 10) * cell_h`.

Rotations are clockwise: r90 turns the top of the picture to the right.

| File | Size | Cell | From | Scale | Turn | Tiles | Used for |
|---|---|---|---|---|---|---|---|
| `tiles_hand2_a.bmp` | 840x224 | 84x112 | hand | 2 | - | 0..19 (10 x 2) | own hand |
| `tiles_hand2_b.bmp` | 840x224 | 84x112 | hand | 2 | - | 20..37 as 0..17 (n - 20) | own hand |
| `tiles_river2_r0.bmp` | 480x256 | 48x64 | river | 2 | 0 | 0..37 | own river and melds, the hands in a result |
| `tiles_river2_r90.bmp` | 640x192 | 64x48 | river | 2 | 90 | 0..37 | the left player's river |
| `tiles_river2_r180.bmp` | 480x256 | 48x64 | river | 2 | 180 | 0..37 | the player across: river |
| `tiles_river2_r270.bmp` | 640x192 | 64x48 | river | 2 | 270 | 0..37 | the right player's river; own sideways tiles (riichi, melds) |
| `tiles_river_r90.bmp` | 320x96 | 32x24 | river | 1 | 90 | 0..37 | the left player's melds |
| `tiles_river_r180.bmp` | 240x128 | 24x32 | river | 1 | 180 | 0..37 | the player across: melds |
| `tiles_river_r270.bmp` | 320x96 | 32x24 | river | 1 | 270 | 0..37 | the right player's melds |
| `tiles_side.bmp` | 72x32 | see below | hand (colours of the back) | - | - | - | the opponents' hands |

`tiles_side.bmp` holds the side of a concealed tile seen standing, in the
colours of the back tile of the hand sheet: across (0, 0, 32x20), left
(32, 0, 20x32), right (52, 0, 20x32).

Which turn a seat uses: own r0, the player on the left r90, across r180, on
the right r270. A tile laid sideways (a riichi tile, the called tile of a
meld) takes the seat's turn plus 270. The upright small sheet (r0) is
`../tiles_river.bmp` itself.

The boards (Tab5, P4-Nano) leave the 2x sheets, the sides and this file
out of their images (`config/storage_exclude_tab5.txt`,
`storage_exclude_naryav4.txt`): their apps get at most 640x360. They keep
the three turned small sheets (`tiles_river_r90/r180/r270.bmp`, about 95 KB):
the 640x360 spectators' screen (`../mahjong_watch.rb`) turns each hand to
its player with them.
