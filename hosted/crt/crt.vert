// crt.vert - the CRT look's vertex shader (doc/web_crt/): one triangle
// over the whole target. GLSL ES 3.00 without its #version line, which the
// users put first: the browser build (wasm/web/crt.js, written in by
// rake webcrt:gen) and the native compositor (native/wlr/wl_crt.c, through
// a header CMake makes).
in vec2 a_pos;
void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }
