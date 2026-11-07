# crt_shaders.cmake - hosted/crt/crt.vert and crt.frag as C strings for the
# compositor's CRT look (native/wlr/wl_crt.c), run by the build:
#   cmake -DVERT=.. -DFRAG=.. -DOUT=crt_shaders.h -P crt_shaders.cmake
function(crt_c_string var path out)
  file(READ ${path} text)
  string(REPLACE "\\" "\\\\" text "${text}")
  string(REPLACE "\"" "\\\"" text "${text}")
  string(REPLACE "\n" "\\n\"\n\"" text "${text}")
  set(${out} "static const char ${var}[] =\n\"${text}\";\n" PARENT_SCOPE)
endfunction()
crt_c_string(crt_vert_src ${VERT} v)
crt_c_string(crt_frag_src ${FRAG} f)
file(WRITE ${OUT} "/* Generated from hosted/crt/ by native/wlr/crt_shaders.cmake; do not edit. */\n#pragma once\n\n${v}\n${f}")
