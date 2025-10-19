MRuby::Gem::Specification.new('picoruby-machine') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'Machine class'

  spec.add_dependency 'picoruby-io-console'

  # Add include directory for hal.h
  spec.cc.include_paths << "#{dir}/include"
  spec.cc.flags << "-I#{dir}/include"

  spec.posix

  if build.posix?
    cc.defines << "PICORB_PLATFORM_POSIX"
  end
  # Note: ESP32用のmachine.cはCMakeLists.txtのPICORUBY_SRCSでコンパイルされる
  # mrbgemビルドでは追加しない(FreeRTOSヘッダーが必要なため)

  if build.gems.map(&:name).include?('picoruby-mruby')
    # Workaround:
    #   Locate mruby-io at the top of gem_init.c
    #   to define IO.open earlier than this gems
    if build.posix?
      spec.add_dependency 'mruby-io'
    end
  end
end
