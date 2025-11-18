MRuby::Gem::Specification.new('picoruby-machine') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'Machine class'

  spec.add_dependency 'picoruby-io-console'

  # Add include directory for hal.h
  spec.cc.include_paths << "#{dir}/include"
  spec.cc.flags << "-I#{dir}/include"

  # Note: ESP32用のmachine.cはCMakeLists.txtのPICORUBY_SRCSでコンパイルする
  # 以下有効にすると、ports/posix 以下の自動的に対象になる
  #spec.posix

  if build.posix?
    cc.defines << "PICORB_PLATFORM_POSIX"
  end

  # ホストビルド(mrbcコンパイラ)の判定
  # mruby-bin-mrbc2が含まれている場合のみコンパイラビルドと判定
  # (Linuxターゲットビルドもbuild.name=='host'だが、mruby-bin-mrbc2を含まない)
  if build.gems.map(&:name).include?('mruby-bin-mrbc2')
    puts "[mrbgem.rake] picoruby-machine: mruby-bin-mrbc2 FOUND - defining PICORUBY_HOST_BUILD"
    puts "[mrbgem.rake]   build.name=#{build.name}"
    puts "[mrbgem.rake]   gems=#{build.gems.map(&:name).join(',')}"
    cc.defines << "PICORUBY_HOST_BUILD"
  else
    puts "[mrbgem.rake] picoruby-machine: mruby-bin-mrbc2 NOT FOUND - NOT defining PICORUBY_HOST_BUILD"
    puts "[mrbgem.rake]   build.name=#{build.name}"
    puts "[mrbgem.rake]   gems count=#{build.gems.map(&:name).length}"
  end


  if build.gems.map(&:name).include?('picoruby-mruby')
    # Workaround:
    #   Locate mruby-io at the top of gem_init.c
    #   to define IO.open earlier than this gems
    if build.posix?
      spec.add_dependency 'mruby-io'
    end
  end
end
