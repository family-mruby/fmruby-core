# The gems of a device configuration built for the host, for that
# configuration's checkpoint table (doc/checkpoint/flash_table.md, Ff;
# `rake checkpoint:table`).
#
#   FMRB_CKPT_TABLE_OF=esp32 | esp32p4   the configuration (family_mruby_<of>.rb)
#
# The device's own file is read as it is and its build renamed
# (ckpt-table-<of>), so the gem list, the defines and the environment it reads
# (FMRB_ASTERISM, FMRB_IMU, FMRB_HW_TARGET ...) cannot drift from it. Then:
# the host compiler instead of the cross one; 64-bit (the table holds names
# and numbers, the same at every pointer width, so MRB_32BIT is left out);
# and mruby-checkpoint with its safe point hook, which the P4 configuration
# carries itself (F6) and the S3 one does not yet. The gems' posix ports in place of their esp32 ones (which
# need ESP-IDF; the IDF component compiles them on the device), as the Linux
# builds have them: they give the platform's data (the environment, the
# board's name) and define no classes or methods. What the gems call that has
# no posix port (the hardware) is given to the table maker as stand-ins that
# return 0 (tools/ckpt_table/ckpt_table.rb).
of = ENV['FMRB_CKPT_TABLE_OF']
raise "FMRB_CKPT_TABLE_OF must be esp32 or esp32p4 (got #{of.inspect})" unless %w[esp32 esp32p4].include?(of)
path = File.join(__dir__, "family_mruby_#{of}.rb")
name = "ckpt-table-#{of}"
src = File.read(path)
head = %Q[MRuby::CrossBuild.new("#{of}")]
raise "#{path}: no #{head}" unless src.include?(head)
eval(src.sub(head, %Q[MRuby::CrossBuild.new("#{name}")]), binding, path) # rubocop:disable Security/Eval

conf = MRuby.targets[name]
conf.cc.command = 'gcc'
conf.linker.command = 'gcc'
conf.archiver.command = 'ar'
conf.cc.flags = conf.cc.flags.flatten.reject { |f| f.to_s =~ /\A-m(longcalls|arch=|abi=)/ }
conf.cc.defines.delete('MRB_32BIT')
conf.ports :posix
conf.cc.defines << 'PICORB_PLATFORM_POSIX'   # what the posix ports are compiled with
# The P4 configuration carries the gem itself (F6); the S3 one does not yet.
unless conf.cc.defines.include?('FMRB_HAVE_CHECKPOINT')
  conf.cc.defines << 'MRB_VM_SAFEPOINT_HOOK'
  conf.cc.defines << 'FMRB_HAVE_CHECKPOINT'
  conf.gem core: 'mruby-checkpoint'
end
