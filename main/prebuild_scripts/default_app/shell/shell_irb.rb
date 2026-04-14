# Shell IRB mode mixin - Interactive Ruby evaluation

module ShellIrbMixin
  def cmd_irb
    @history << "IRB mode - Type 'exit' or 'quit' to return"
    @need_full_redraw = true
    @irb_mode = true
    @prompt = "irb> "  # Change prompt for IRB mode
    @irb_sandbox = Sandbox.new
    @irb_sandbox.compile("_ = nil")
    @irb_sandbox.execute
    @irb_sandbox.wait(timeout: nil)
    @irb_sandbox.suspend
  end

  def irb_eval(script)
    # Skip empty input
    if script.empty?
      @need_full_redraw = true
      return
    end

    if script == "exit" || script == "quit"
      @irb_mode = false
      @irb_sandbox.terminate if @irb_sandbox
      @irb_sandbox = nil
      @history << "Exited IRB mode"
      @prompt = "> "
      @need_full_redraw = true
      return
    end

    # Capture stdout
    old_stdout = $stdout
    capturer = OutputCapturer.new
    $stdout = capturer

    begin
      # Try to compile and execute the script
      Log.debug("[IRB] Compiling: #{script}")
      if @irb_sandbox.compile("begin; _ = (#{script}); rescue => _; end; _")
        # Execute and get result
        Log.debug("[IRB] Executing...")
        executed = @irb_sandbox.execute
        Log.debug("[IRB] Executed: #{executed}")
        if executed
          Log.debug("[IRB] Waiting...")
          @irb_sandbox.wait(timeout: 5000)
          Log.debug("[IRB] Suspending...")
          @irb_sandbox.suspend
          Log.debug("[IRB] Getting result...")
          result = @irb_sandbox.result
          Log.debug("[IRB] Result: #{result.inspect}")

          # Get captured output
          output = capturer.get_output

          # Display captured output (without debug logs)
          output.each_line do |line|
            next if line.start_with?("[IRB]")
            @history << line.chomp
          end

          # Display result if not nil
          @history << "=> #{result.inspect}" unless result.nil?
        else
          @history << "Error: Execution failed"
        end
      else
        @history << "Error: Compilation failed"
      end
    rescue => e
      Log.error("[IRB] Exception: #{e.message}")
      @history << "Error: #{e.message}"
    ensure
      # Restore stdout
      $stdout = old_stdout
    end

    @need_full_redraw = true
  end
end
