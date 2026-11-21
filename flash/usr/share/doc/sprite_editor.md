# Sprite Editor: draw small pictures and tiles

The Sprite Editor (/app/tool/sprite_editor.app.rb) draws small pictures, one
dot at a time and zoomed in: game characters, or stamps for the chat. Open it
from the launcher.

The screen

- The menu (File, Edit) at the top, the tools on the left, the zoomed picture
  in the middle, the colours on the right and a status line at the bottom.
- The status line shows the file's name (with * when changed), the picture's
  size, the dot under the pointer and the colour in use.

Tools (the left column)

- Pen: hold the button and move to paint in the colour in use. Key P.
- Erase: what you go over becomes transparent. Key E.
- Fill: the area of the same colour joined to the dot you press takes the
  colour in use. Key F.
- Pick: the colour of the dot you press becomes the colour in use (then back
  to the pen). Key I.
- Undo: takes back the last step, up to 10 steps. Ctrl+Z too.

Colours (the right)

- All 256 colours the device can show, in a 16 x 16 grid. Press one to use it.
- The second cell from the top left, a checkerboard, is transparent (colour
  01). Transparent dots show as a checkerboard on the picture too; shown as a
  sprite or a stamp, what is behind shows through them.
- Under the grid are the colours used last, and under them the colour in use
  with its number (hexadecimal).

Files (the File menu)

- New (Ctrl+N): choose 16x16, 24x24, 32x32 or 48x48. It starts all transparent.
- Open (Ctrl+O): choose a PNG or a BMP.
- To open a picture here with a double click in the File Manager, put
  `png = "/app/tool/sprite_editor.app.rb"` (bmp = ... for a BMP) in
  /home/associations.toml.
- Save (Ctrl+S): writes to the file it came from, in its format (PNG or BMP).
  A new picture goes under /home/stamps/ as sprite1.png, sprite2.png ...
  (the folder is made when missing).
- Save as: choose the folder and the name. A name without .png or .bmp gets .png.
- New, Open and closing (the round button, Ctrl+Q) ask first whether to save
  unsaved changes. Cancel goes back to the picture.

Tile sheets

- A picture larger than 48 x 48 opens as a sheet of 16 x 16 tiles. The tiles
  are listed at the bottom right; the one pressed is zoomed in the middle.
  Saving writes back the sheet with the other tiles as they were.
- With many tiles, page through them with the arrows at the right of the list
  or the wheel.

The window

- Drag the bottom-right corner to resize. A larger window gives a larger
  zoom and larger colour cells.
