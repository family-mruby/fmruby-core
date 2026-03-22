MRuby::Gem::Specification.new('mruby-dir') do |spec|
  spec.license = 'MIT and MIT-like license'
  spec.authors = ['Internet Initiative Japan Inc.', 'Kevlin Henney']

  # family-mruby: On ESP32, dir_hal.c is built via CMakeLists.txt (not as a HAL gem).
  # Skip HAL gem auto-detection for cross-builds targeting ESP32.
  unless cc.defines.include?("ESP32_PLATFORM")
    spec.build.gems.one? { |g| g.name =~ /^hal-.*-dir$/ } or begin
      suggested_hal = if ENV['MRUBY_DIR_HAL']
        ENV['MRUBY_DIR_HAL']
      elsif spec.for_windows?
        'hal-win-dir'
      elsif RUBY_PLATFORM =~ /linux|darwin|bsd/
        'hal-posix-dir'
      else
        nil
      end

      if suggested_hal
        warn "mruby-dir: No HAL specified, loading #{suggested_hal} (explicit selection recommended)"
        spec.build.gem core: suggested_hal
      else
        fail "mruby-dir: No HAL available for platform '#{RUBY_PLATFORM}'.\n" \
             "Please specify HAL gem explicitly in your build config:\n" \
             "  conf.gem core: 'hal-posix-dir'   # For Linux/macOS/BSD\n" \
             "  conf.gem core: 'hal-win-dir'     # For Windows\n" \
             "Or set environment variable:\n" \
             "  MRUBY_DIR_HAL=hal-myplatform-dir\n" \
             "See mrbgems/mruby-dir/README.md for creating custom HAL."
      end
    end
  end
end
