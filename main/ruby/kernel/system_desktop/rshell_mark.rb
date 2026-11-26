# The remote shell's mark for SystemDesktopApp (doc/remote_shell).
#
# Only the machines the remote shell is for carry this file: Modern (Tab5,
# P4-Nano) and the browser build. A Retro firmware (and the Retro sim) builds
# the desktop with system_desktop_retro/rshell_mark.rb instead, the same
# three methods doing nothing, so the desktop's bytecode there is as it was
# before the mark (main/CMakeLists.txt, FMRB_MIXIN_EXCLUDE_system_desktop).
#
# The mark sits left of the kana cell, only while a session is open: "RS" on
# the theme's highlight. Not clickable (ending a session from here is not
# decided). Without it the bar is as it was; with it the RAM readout and the
# battery move left by rshell_pad, and the taskbar stops that much sooner.
# The cell is 14 px and 4 px of gap (literals, not a constant: on Spinel a
# constant is a static in internal RAM).
module RshellMarkMixin
  def rshell_pad
    @rshell_on ? 18 : 0
  end

  # The desktop's constants through self.class: a module method does not
  # find the including class's constants by name.
  def draw_rshell_icon
    return unless @rshell_on
    x = @window_width - self.class::STATUS_RIGHT - self.class::WIFI_ICON_W - 7 - self.class::BLE_CELL_W - 1 - 4 -
        self.class::KANA_CELL_W - 4 - 14
    @gfx.fill_rect(x, 1, 14, 10, FmrbConst::THEME_HIGHLIGHT)
    @gfx.draw_text(x + 1, 2, "RS", FmrbGfx::WHITE, FmrbConst::THEME_HIGHLIGHT)
  end

  # A remote shell session opened or ended (the transfer app, through the
  # kernel's rshell_state): the mark goes up or down and the bar is drawn
  # again.
  def rshell_state_changed(msg)
    on = msg["on"] == true
    if on != @rshell_on
      @rshell_on = on
      draw_foreground if @boot_anim_state == :done && !@direct_boot
    end
  end
end
