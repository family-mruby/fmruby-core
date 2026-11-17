require 'erb'

MRuby::Gem::Specification.new('picoruby-require') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'PicoRuby require gem'

  spec.add_dependency 'picoruby-sandbox'
  spec.add_dependency 'picoruby-fmrb-filesystem'

  if build.femtoruby?
    if build.posix?
      # TODO: in Wasm, you may need to implement File class with File System Access API
      spec.add_dependency 'picoruby-posix-io'
    else
      spec.add_dependency 'picoruby-vfs'
      spec.add_dependency 'picoruby-littlefs'
    end
  end

  mrbgems_dir = File.expand_path "..", build_dir

  # picogems to be required in Ruby
  picogems = Hash.new
  mgems = Array.new
  task :collect_gems => "#{mrbgems_dir}/gem_init.c" do
    build.gems.each do |gem|
      if gem.name.start_with?("picoruby-") && !gem.name.start_with?("picoruby-bin-")
        gem_name = gem.name.sub(/\Apicoruby-?/,'')
        mrbfile = "#{mrbgems_dir}/#{gem.name}/mrblib/#{gem_name}.c"
        src_dir = "#{gem.dir}/src"
        initializer = if Dir.exist?(src_dir) && !Dir.empty?(src_dir)
                        "mrbc_#{gem_name}_init".gsub('-','_')
                      else
                        "NULL"
                      end
        picogems[gem.require_name || File.basename(mrbfile, ".c")] = {
          mrbfile: mrbfile,
          initializer: initializer
        }
        rbfiles = Dir.glob("#{gem.dir}/mrblib/**/*.rb").sort
        unless rbfiles.empty?
          file mrbfile => rbfiles do |t|
            mkdir_p File.dirname(t.name)
            File.open(t.name, 'w') do |f|
              name = "picogem_#{File.basename(t.name, ".c").gsub('-','_')}"
              mrbc.run(f, t.prerequisites, name, cdump: false)
              if initializer != "NULL"
                f.puts
                f.puts "void #{initializer}(mrbc_vm *vm);"
              end
            end
          end
        end
      elsif gem.name.start_with?("mruby-")
        mgems << gem.name.sub(/\Amruby-?/,'')
      end
    end
  end

  build.libmruby_objs << objfile("#{mrbgems_dir}/picogem_init")
  file objfile("#{mrbgems_dir}/picogem_init") => ["#{mrbgems_dir}/picogem_init.c"]

  # fmrb: picogem_init.c is written only when it changes, and what depends on
  # it no longer sees :collect_gems. That plain task's timestamp is "now",
  # and Rake looks at every prerequisite recursively (all_prerequisite_tasks),
  # so picogem_init.pi/.o and libmruby were rebuilt on every rake run. The
  # generation runs every time from a proxy whose timestamp is the file's own
  # (the shape tasks/presym.rake uses, mruby/mruby#6721).
  picogem_init_c = "#{mrbgems_dir}/picogem_init.c"
  picogem_gen = task "picogem_init:gen:#{build.name}" => [MRUBY_CONFIG, __FILE__, :collect_gems] do
    picogems.each do |_require_name, v|
      if Rake::Task.task_defined?(v[:mrbfile])
        Rake::FileTask[v[:mrbfile]].invoke
      end
    end
    template_path = if build.picoruby?
                      File.join(spec.dir, "templates/mruby/picogem_init.c.erb")
                    else
                      File.join(spec.dir, "templates/mrubyc/picogem_init.c.erb")
                    end
    template = ERB.new(File.read(template_path), trim_mode: "%-")
    content = template.result(binding)
    unless File.exist?(picogem_init_c) && File.read(picogem_init_c) == content
      File.write(picogem_init_c, content)
    end
  end
  picogem_proxy = task "picogem_init:update:#{build.name}" do
    picogem_gen.invoke
  end
  picogem_proxy.define_singleton_method(:timestamp) do
    File.exist?(picogem_init_c) ? File.mtime(picogem_init_c) : Rake::LATE
  end
  file picogem_init_c => picogem_proxy
end

