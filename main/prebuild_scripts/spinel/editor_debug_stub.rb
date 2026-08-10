# No-op EditorDebugPane for the Spinel build of the editor.
#
# The mruby build concatenates main/prebuild_scripts/default_app/editor/
# debug_pane.rb (the real on-device debugger UI); the Spinel build takes this
# file instead, so FMRB::Debug and everything around it is statically absent --
# there is no runtime switch and no #ifdef in the editor source, only a different
# file in the manifest. Debugging is done by running the mruby build of the
# editor (doc/editor_serious_mode/instruction_p5.md, user decision 2026-08-10).
#
# Every method the editor body may call on the debugger lives here, answering
# "no session, nothing to draw, key not consumed".
module EditorDebugPane
  def dbg_init
  end

  def dbg_active?
    false
  end

  def dbg_pane_h
    0
  end

  def dbg_gutter_w
    0
  end

  def dbg_draw_pane(y0, h)
  end

  def dbg_modal?
    false
  end

  def dbg_draw_modal
  end

  def dbg_handle_modal_key(ev)
    false
  end

  def dbg_handle_key(ev)
    false
  end

  def dbg_menu_visible?
    false
  end

  def dbg_menu_label
    ""
  end

  def dbg_menu_width
    0
  end

  def dbg_menu_items
    []
  end

  def dbg_activate_item(idx)
  end

  def dbg_line_background(line_idx)
    self.class::BG_COLOR
  end

  def dbg_draw_gutter(line_idx, y)
  end

  def dbg_poll
  end

  def dbg_shutdown
  end
end
