# rakelib/zenoh.rake
# zenoh-pico (the C library under the picoruby-zenoh gem): fetch the pinned
# release. Part of the Rakefile split: shared constants and helpers live in the
# top-level Rakefile, which Rake loads before every file in rakelib/.

namespace :zenoh do
  desc "Fetch the pinned zenoh-pico release into vendor/zenoh-pico"
  task :setup do
    pin = zenoh_pico_pin
    dir = ZENOH_PICO_VENDOR_DIR
    if Dir.exist?(File.join(dir, ".git"))
      head = `git -C #{dir} rev-parse HEAD 2>/dev/null`.strip
      unless head == pin["commit"]
        sh "git -C #{dir} fetch --depth 1 origin #{pin["commit"]}"
        sh "git -C #{dir} checkout --detach #{pin["commit"]}"
      end
    else
      mkdir_p File.dirname(dir)
      ref = pin["tag"] || pin["branch"] || "main"
      sh "git clone --branch #{ref} --depth 1 #{pin["repo"]} #{dir}"
      head = `git -C #{dir} rev-parse HEAD`.strip
      unless head == pin["commit"]
        sh "git -C #{dir} fetch --depth 1 origin #{pin["commit"]}"
        sh "git -C #{dir} checkout --detach #{pin["commit"]}"
      end
    end
    head = `git -C #{dir} rev-parse HEAD`.strip
    abort "zenoh-pico is at #{head}, expected #{pin["commit"]}" unless head == pin["commit"]
    puts "zenoh-pico ready: #{dir} (#{pin["tag"]} #{head[0, 12]})"
  end
end
