# The remote shell's mark, left out of a Retro desktop (it stands in for
# system_desktop/rshell_mark.rb there; main/CMakeLists.txt). The remote shell
# is for Modern and the browser build (doc/remote_shell), and the mark's code
# was 4.7 KB of the Retro desktop's VM pool. The bar never moves over.
module RshellMarkMixin
  def rshell_pad
    0
  end

  def draw_rshell_icon
  end

  def rshell_state_changed(msg)
  end
end
