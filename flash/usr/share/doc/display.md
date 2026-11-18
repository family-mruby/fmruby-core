# Screen resolution

The screen of a Modern machine (P4-Nano, Tab5) has two resolutions to choose
from.

- Standard (426x240): the one it starts with. Large text and large targets;
  the one for the Tab5's own panel.
- High (640x360): 2.25 times as much on one screen, for putting the editor
  and other windows side by side on an HDMI monitor. Text looks 2/3 the size.

## Switching

Open Config from the menu, pick Display > Resolution with < >, and press
Save & Reboot. The machine comes back at the new resolution.

- At High, the fullscreen editor is shown at 640x360 without switching.
- Fullscreen apps made for 426x240 (games and the like) switch the screen to
  426x240 while they are in front, so they look as they do at Standard. It
  goes back to 640x360 when they close or return to a window. The picture may
  glitch for a moment at the switch.
- Apps that lay themselves out at any size (Shooter, PicoRabbit and others)
  stay at 640x360 without switching. For an app of your own, write
  `fullscreen_any_size = true` in its .app.toml.
- The mouse moves the same distance on the monitor at either resolution
  (the mouse scale on Config's Input page applies as it is).

## When the screen does not come up (the way back)

- **Hold Shift while the machine starts (power on or reset)** and it starts
  at Standard (426x240) that one time, whatever the setting says. The setting
  is not changed: pick it again in Config. The keyboard has to be plugged
  into the machine itself.
- From a PC: over the WiFi remote control, set display_width = 426 and
  display_height = 240 in /etc/system_conf.toml and restart (the development
  tool tools/fmrb_rd_fs.rb: get, put, reboot).
