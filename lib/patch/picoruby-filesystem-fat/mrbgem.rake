MRuby::Gem::Specification.new('picoruby-filesystem-fat') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'FAT filesystem'

  if build.vm_mrubyc?
    spec.add_dependency 'picoruby-time-class'
  elsif build.vm_mruby?
    spec.add_dependency 'mruby-time'
  end

  # Define FMRB_TARGET_ESP32 for ESP32 builds to exclude FatFS dependencies
  if build.name == 'esp32'
    spec.cc.defines << 'FMRB_TARGET_ESP32'
  end

  # TODO: use #porting instead
  # Skip FatFS compilation for ESP32 as ESP-IDF provides its own FatFS implementation
  unless build.name == 'esp32'
    Dir.glob("#{dir}/src/hal/*.c").each do |src|
      obj = "#{build_dir}/src/#{objfile(File.basename(src, ".c"))}"
      file obj => src do |t|
        cc.run(t.name, t.prerequisites[0])
      end
      objs << obj
    end

  end

end

