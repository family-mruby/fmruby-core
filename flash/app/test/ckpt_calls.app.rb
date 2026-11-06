# A windowless app that can be moved and calls a method only some builds have
# (doc/checkpoint F4, doc/checkpoint/movable_apps.md). FmrbApp.linux_settings
# is the native program's (the Linux settings window); the simulation has no
# such method. The app guards the call with respond_to? and never makes it
# (its flag is never set): the call is in its code all the same, so an image
# names it (SND0) and a build without it says so when the image arrives, and
# lets it go on. ckpt_requires.app.rb is the same with the name declared as
# required in its .app.toml: there the build refuses it.
#
# It also logs what the base answers from the machine it runs on, which must be
# the machine where it arrived, not the one that wrote it (report/
# platform_restore.md): platform beside FmrbConst::PLATFORM, whether the app
# keeps @platform (it must not: the image would carry it), and whether
# FmrbAssoc's table is still cached (read here in on_create, forgotten when
# the image is restored). And the display language (report/lang_bg_restore.md):
# FmrbI18n's language, asked here before the move, a word looked up in it,
# and FmrbApp.language; after a move all three are the arriving machine's.

class CkptCallsApp < FmrbApp
  def on_create
    @main = 0
    @open_settings = false
    FmrbAssoc.load_table
    FmrbI18n.add("en" => { ckpt_word: "word-en" }, "ja" => { ckpt_word: "word-ja" })
    FmrbI18n.lang
    Log.info("ckpt_calls: start #{machine}")
  end

  def on_update
    @main += 1
    have = FmrbApp.respond_to?(:linux_settings)
    FmrbApp.linux_settings if @open_settings && have
    Log.info("ckpt_calls: main=#{@main} pack=#{[65].pack('C')} linux_settings=#{have} #{machine}")
    250
  end

  def on_restore
    Log.info("ckpt_calls: on_restore main=#{@main} #{machine}")
  end

  def machine
    ivar = instance_variables.include?(:@platform)
    assoc = FmrbAssoc.instance_variable_get(:@table) ? true : false
    "platform=#{platform} const=#{FmrbConst::PLATFORM} platform_ivar=#{ivar} assoc_cached=#{assoc} " \
      "lang=#{FmrbI18n.lang} word=#{FmrbI18n.t(:ckpt_word)} language=#{FmrbApp.language}"
  end
end

begin
  app = CkptCallsApp.new
  app.start
rescue => e
  Log.error("ckpt_calls: #{e.class}: #{e.message}")
end
