# frozen_string_literal: true
#
# targets.rb - the boards the draw-record comparison (rake regress) draws
# every scene for. One line per board. Adding a board is adding a line here:
# its screen joins every scene that applies to it, and its records go under
# test/regress/baseline/ the next time someone runs rake regress:update.
#
#   name    the board (FMRB_HW_TARGET's spelling, lower case)
#   family  FmrbConst::HW_FAMILY: "modern", "retro" or "mini"
#   screen  [w, h] the display (system_conf display_width/height)
#   hires   [w, h] the high-resolution fullscreen screen (fullscreen_hires
#           apps start at this size), or nil where there is none
#   margin  [x, y] system_conf display_margin_x/y: a fullscreen app gets the
#           screen minus this
#   app     [w, h] system_conf default_user_app_width/height (the window of
#           an .app.toml without a size)
#   font_ja false for a build without the efontJA fonts (FMRB_FONT_JA=0:
#           misaki 8 only)
#   conf    config/<conf>: copied to /etc/system_conf.toml for the scene
#           (theme, language), with the screen lines checked against `screen`
#   exclude config/<exclude>: the board's storage_exclude list (nil: none).
#           An app whose file the board leaves out is not drawn for it, so a
#           new board's list says by itself which apps it runs.
#
# Boards with the same screen (everything above but name and conf) draw the
# same picture, so they share one set of records: the profile. Tab5,
# NARYAv4 and the browser build are one profile; a new board with a new
# screen size is a new profile.
module Regress
  Target = Struct.new(:name, :family, :screen, :hires, :margin, :app, :font_ja, :conf, :exclude, keyword_init: true) do
    # What decides the picture. Two targets with the same key draw the same.
    def profile_key
      [family, screen, hires, margin, app, font_ja]
    end

    # The fullscreen size an app gets (hires only for a fullscreen_hires app).
    def fullscreen_size(hires_app)
      return hires if hires_app && hires
      [screen[0] - margin[0], screen[1] - margin[1]]
    end

    # Whether the board's storage carries the file ("flash/app/demo/x.app.rb").
    def carries?(file, core)
      return true if exclude.nil? || !file.start_with?("flash/")
      @excluded ||= File.readlines(File.join(core, "config", exclude)).map { |l| l.sub(/#.*/, "").strip }.reject(&:empty?)
      rel = file.sub(%r{\Aflash/}, "")
      @excluded.none? { |e| rel == e || rel.start_with?(e.chomp("/") + "/") }
    end
  end

  TARGETS = [
    Target.new(name: "narya_v3", family: "retro", screen: [320, 240], hires: nil, margin: [2, 16], app: [280, 180],
               font_ja: true, conf: "system_conf_n16r8.toml", exclude: "storage_exclude_naryav3.txt"),
    Target.new(name: "tab5", family: "modern", screen: [426, 240], hires: [640, 360], margin: [0, 0], app: [386, 180],
               font_ja: true, conf: "system_conf_p4.toml", exclude: "storage_exclude_tab5.txt"),
    Target.new(name: "naryav4", family: "modern", screen: [426, 240], hires: [640, 360], margin: [0, 0], app: [386, 180],
               font_ja: true, conf: "system_conf_naryav4.toml", exclude: "storage_exclude_naryav4.txt"),
    Target.new(name: "wasm", family: "modern", screen: [426, 240], hires: [640, 360], margin: [0, 0], app: [386, 180],
               font_ja: true, conf: "system_conf_wasm.toml"),
    Target.new(name: "sticks3", family: "mini", screen: [240, 135], hires: nil, margin: [0, 0], app: [240, 135],
               font_ja: false, conf: "system_conf_sticks3.toml", exclude: "storage_exclude_sticks3.txt"),
    Target.new(name: "atoms3r", family: "mini", screen: [128, 128], hires: nil, margin: [0, 0], app: [128, 128],
               font_ja: false, conf: "system_conf_atoms3r.toml", exclude: "storage_exclude_atoms3r.txt")
  ].freeze
end
