
# Appended to every built-in app (*.app.rb) when its bytecode is built
# (compile_ruby_to_bytecode.cmake, rakelib/hosted.rake, rakelib/native.rake):
# starts the FmrbApp subclass the app defines, in the program's own Task and
# at the depth the old start code ran, as fmrb_app.c does for an app loaded
# from a file (doc/codebase_tidy T3).
(__fmrb_app = FmrbApp._app_to_start).is_a?(Class) ? __fmrb_app.new.start : __fmrb_app
