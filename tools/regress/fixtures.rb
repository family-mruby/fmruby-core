# frozen_string_literal: true
#
# fixtures.rb - the fixed sets the screen scenes and the sim's VM pool run
# show instead of the real flash/app, so adding an app does not move their
# baselines (doc/regression/usage.md 9.4, report/hardening.md).
#
#   launcher  test/regress/fixtures/launcher_index: the launcher's index cache
#             (/var/cache/launcher_index, launcher.rb load_launcher_cache) with
#             a dozen real apps. The desktop replays a cache at boot instead of
#             walking /app, so a cache put down before the boot is what the
#             launcher lists (after the built-in entries, which are not
#             cached). The sim reads it from flash/var/cache of this checkout,
#             the native program from its cache directory, the browser build
#             from the page's seed (fmrb_web.rb seed).
#   files     a small tree under /tmp/regress_fm and the File Manager's state
#             (/tmp/file_manager.state.toml) pointing at it: the File Manager
#             opens there, so its scenes list these files and never /app.
require "fileutils"

module Regress
  module Fixtures
    CORE = File.expand_path("../..", __dir__)
    DIR = File.join(CORE, "test", "regress", "fixtures")
    LAUNCHER_INDEX = File.join(DIR, "launcher_index")
    LAUNCHER_CACHE_VPATH = "/var/cache/launcher_index"

    FM_DIR = "/tmp/regress_fm"
    FM_STATE_VPATH = "/tmp/file_manager.state.toml"
    # name => content (a name ending in / is a directory); fixed sizes, so the
    # listing's size column does not move
    FM_FILES = {
      "#{FM_DIR}/alpha/notes.txt" => "regression fixture\n" * 6,
      "#{FM_DIR}/alpha/hello.app.rb" => "# a fixture, never run\nputs 'hello'\n",
      "#{FM_DIR}/alpha/picture.bmp" => "B" * 2048,
      "#{FM_DIR}/alpha/sub/" => nil,
      "#{FM_DIR}/beta/" => nil,
      "#{FM_DIR}/gamma.txt" => "gamma\n"
    }.freeze

    module_function

    def launcher_index = File.binread(LAUNCHER_INDEX)

    # The File Manager's state as save_app_state writes it.
    def fm_state(dir = FM_DIR)
      "# remembered by file_manager; delete this file to forget\ndir = \"#{dir}\"\n"
    end

    # Write the File Manager's tree under a host directory that stands for
    # the machine's "/" (or whatever root maps /tmp there).
    def write_fm_tree(root)
      FileUtils.rm_rf(File.join(root, FM_DIR))
      FM_FILES.each do |vpath, body|
        path = File.join(root, vpath)
        if vpath.end_with?("/")
          FileUtils.mkdir_p(path)
        else
          FileUtils.mkdir_p(File.dirname(path))
          File.binwrite(path, body)
        end
      end
    end
  end
end
