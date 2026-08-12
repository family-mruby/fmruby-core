# File load/save and Run (F5) for the editor, plus the transient status-line
# helpers they share. Split out of editor.app.rb (doc/editor_refactor).
# self.class::STATUS_MSG_FRAMES is the editor body's; reached as self.class::NAME.
module EditorFileRun

  # ---- File operations ----

  # EditorCore reads the file straight into its arena in chunks, so nothing here
  # holds the contents: no whole-file String, no Array of lines. A negative
  # return means the file could not be read or the arena is full -- the editor
  # says so and keeps the buffer it had.
  def load_file(path)
    n = EditorCore.load_file(path)
    if n < 0
      if n == -2
        flash_status(FmrbI18n.t(:b_too_large).to_s)
        Log.error("Load failed (document arena full): #{path}")
      else
        flash_status(FmrbI18n.t(:b_load_failed).to_s)
        Log.error("Failed to load file '#{path}' (err=#{n})")
      end
      @need_redraw = true
      return
    end
    @cx = 0
    @cy = 0
    @scroll_y = 0
    @scroll_x = 0
    @modified = false
    # Highlight default is per buffer: a manual toggle on the previous file is
    # not carried over. Size plays no part any more (the highlight cache in
    # EditorCore is per line).
    @hl_manual = false
    @hl_enabled = hl_default_for(path)
    apply_hl_enabled
    @current_file = path
    @need_redraw = true
    # One line with both numbers, so the same measurement is available in the sim
    # and on the device: how much of THIS VM's mruby pool the open file costs
    # (percent) versus how much of the document arena it takes (bytes).
    Log.info("edit_doc: lines=#{n} bytes=#{EditorCore.doc_bytesize} arena=#{EditorCore.mem_used} pool=#{FmrbApp.pool_usage}% file=#{path}")
  end

  # Shown when the arena cannot grow: the editor stays alive and editable, which
  # is the whole point of returning an error code instead of aborting.
  def doc_full
    flash_status(FmrbI18n.t(:b_doc_full).to_s)
    Log.error("Editor document arena full (#{EditorCore.mem_used} bytes used)")
    @need_redraw = true
  end

  def save_file
    unless @current_file
      # A buffer with no name yet (the editor started empty): ask for one rather
      # than failing silently. Ctrl-S used to do nothing at all here, so the only
      # way to save a new file was to know about File > Save as.
      Log.info("Save: no file name yet, asking for one")
      @pending_file_op = :save
      request_file_select("save")
      return
    end

    expected = EditorCore.doc_bytesize
    written = EditorCore.save_file(@current_file)
    if written < 0
      flash_status(FmrbI18n.t(:b_save_failed).to_s)
      Log.error("Failed to save file: #{@current_file} (err=#{written})")
    elsif written != expected
      flash_status(FmrbI18n.t(:b_save_failed).to_s)
      Log.error("Save mismatch for #{@current_file}: expected=#{expected}, written=#{written}")
    else
      @modified = false
      flash_status(FmrbI18n.t(:b_saved).to_s, true)  # green: a save that worked
      Log.info("Saved file: #{@current_file} (#{written} bytes)")
      # A save is the natural moment to check the file: the text has just
      # stopped moving, and the reply keeps the "Saved" message unless there is
      # something to say.
      diagnose_after_save
    end
  end


  # ---- Run (F5) ----

  # Run the file in the buffer, replacing what the last RUN started.
  #
  # The kernel does the work: an app cannot spawn another app, so this sends a
  # run request (see FmrbApp#request_run). The kernel stops @run_pid first, and
  # spawning is what hands the keyboard to the new app -- coming back here is
  # then a matter of closing it, or Alt-Tab style window switching for a
  # windowed app. A fullscreen .bas app covers the editor until it ends.
  def run_current_file
    if @current_file.nil?
      # Nothing on disk yet: name it first and run once the save lands.
      @run_after_save = true
      @pending_file_op = :save
      request_file_select("save")
      return
    end
    unless runnable_path?(@current_file)
      flash_status(FmrbI18n.t(:b_run_path).to_s)
      return
    end
    save_file if @modified
    request_run(@current_file, @run_pid)
    flash_status(FmrbI18n.t(:b_running).to_s)
  end

  # What the spawner can load: any absolute path, wherever the buffer was
  # saved. The kernel enforces the same rule (run_path_allowed?); checking here
  # just gives a better message than a silent no-op.
  def runnable_path?(path)
    path.start_with?("/")
  end

  # Put a message in the status line's message zone -- the only way anything
  # writes there. It clears on the key after the one that raised it, or after
  # self.class::STATUS_MSG_FRAMES ticks, whichever comes first. +ok+ marks the green
  # success flavour (Save uses it).
  def flash_status(text, ok = false)
    @status_msg = " #{text} "
    @status_msg_ok = ok
    @status_msg_frames = self.class::STATUS_MSG_FRAMES
    # The key that raised this message must not also clear it.
    @status_msg_fresh = true
    @dirty_status = true
  end

  def clear_status_message
    return if @status_msg.nil?
    @status_msg = nil
    @status_msg_ok = false
    @status_msg_frames = 0
    @dirty_status = true
  end

  def save_file_as(path)
    # Naming a buffer (or renaming it) re-decides the highlight default, unless
    # the user has already made that call for this buffer.
    unless @hl_manual
      @hl_enabled = hl_default_for(path)
      apply_hl_enabled
    end
    @current_file = path
    save_file
  end

end
