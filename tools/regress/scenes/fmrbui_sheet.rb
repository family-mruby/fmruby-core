# frozen_string_literal: true
#
# A page of every FmrbUI widget (lib/add/picoruby-fmrb-app/mrblib/fmrb-ui.rb),
# written here rather than taken from an app: no shipped mruby app has a
# text field, and the long-text cases (what app_refactor R6 makes FmrbUI
# fit with FmrbText) need texts that are longer than their widgets on
# purpose, in English and in Japanese, at text size 1 and 2.

module FmrbUISheet
  TEXTS = {
    normal: {
      "en" => { title: "Settings", ok: "OK", toggle: "Sound", toggle_on: "Sound: on", field: "name",
                options: %w[Low Mid High], big: "Big" },
      "ja" => { title: "設定", ok: "決定", toggle: "音", toggle_on: "音: 入", field: "なまえ",
                options: %w[低 中 高], big: "大" }
    },
    long: {
      "en" => { title: "A label whose text is far longer than the widget it sits in",
                ok: "Apply the changes now", toggle: "Background music and effects",
                toggle_on: "Background music and effects: on", field: "a_very_long_file_name_typed_in.txt",
                options: ["The quietest setting", "Somewhere in between", "As loud as it goes"], big: "Much too big" },
      "ja" => { title: "部品の幅よりずっと長い文字の見出しがここに入る", ok: "変更をすぐに反映する",
                toggle: "背景の音楽と効果音を鳴らす", toggle_on: "背景の音楽と効果音を鳴らす: 入",
                field: "とても長いファイルの名前です.txt", options: %w[いちばん小さい音 中くらいの音 いちばん大きな音],
                big: "大きすぎる文字" }
    }
  }.freeze

  module_function

  # The app (read in the run, before the scene).
  def define
    Object.const_set(:FmrbUISheetApp, Class.new(FmrbApp) do
      def on_create
        t = FmrbUISheet::TEXTS.fetch(Regress::Env.extra[:sheet] || :normal).fetch(FmrbApp.language)
        @ui = FmrbUI.new(self)
        @ui.label(:title, 4, 4, 160, 10, t[:title])
        @ui.label(:centre, 4, 16, 160, 10, t[:title], align: :center)
        @ui.label(:big, 170, 4, 100, 18, t[:big], text_size: 2)
        @ui.button(:ok, 4, 30, 70, 16, t[:ok])
        @ui.button(:accent, 80, 30, 70, 16, t[:ok], accent: 0xE0)
        @ui.toggle(:snd, 4, 50, 120, 16, t[:toggle], on_text: t[:toggle_on])
        @ui.toggle(:g1, 130, 50, 40, 16, t[:options][0], group: :lvl, on: true)
        @ui.toggle(:g2, 172, 50, 40, 16, t[:options][1], group: :lvl)
        @ui.stepper(:num, 4, 70, 80, 14, 5, 0, 10)
        @ui.enum(:lvl, 90, 70, 120, 14, t[:options], index: 1)
        @ui.text_field(:name, 4, 90, 150, 12, t[:field], max: 48)
        @ui.scrollbar(:sb, 250, 30, 10, 120, 40, 10, 5)
        @ui.button(:off, 4, 110, 70, 16, t[:ok])
        @ui.set_enabled(:off, false)
        clear_user_area
        @ui.flush
      end

      def on_event(ev)
        @ui.handle(ev)
        @ui.flush
      end

      def on_update = 100
    end)
  end
end

Regress.app "fmrbui_sheet", nil do
  window w: 300, h: 160, name: "FmrbUI"
  klass "FmrbUISheetApp"
  inline { FmrbUISheet.define }

  scene "normal_en" do |s|
    s.start
  end

  scene "normal_ja", lang: "ja" do |s|
    s.start
  end

  scene "long_en" do |s|
    s.env.extra[:sheet] = :long
    s.start
  end

  scene "long_ja", lang: "ja" do |s|
    s.env.extra[:sheet] = :long
    s.start
  end

  scene "operate" do |s|
    s.start
    s.tap(:ok)
    s.tap(:snd)
    s.tap(:g2)
    s.tap(:num, at: :right)
    s.tap(:lvl, at: :right)
    s.tap(:sb, at: :middle)
    s.tap(:name)
    s.type("abc")
    s.key :backspace
  end

  scene "operate_long_ja", lang: "ja" do |s|
    s.env.extra[:sheet] = :long
    s.start
    s.tap(:snd)
    s.tap(:lvl, at: :right)
    s.tap(:lvl, at: :right)
    s.tap(:name)
    s.type("xyz")
  end
end
