#!/usr/bin/env ruby
# frozen_string_literal: true
#
# The CRT look's curve in C (hosted/crt/crt_warp.c, the native compositor's
# pointer) against crt.js's (the browser's clicks, and the same formula as
# the shader): both are asked the same points, both ways, and must agree.
#
#   ruby test/web_crt/warp_c_test.rb     (rake webcrt:test, part of rake test)
require "tmpdir"
require "open3"
require "json"

core = File.expand_path("../..", __dir__)
crt_dir = File.join(core, "hosted/crt")

# The points: a grid over 0..1 with the edges, a little outside, and the
# strengths the sliders give (0, the default 15, 40, 100).
points = []
(-2..42).each { |i| (-2..42).each { |j| points << [i / 40.0, j / 40.0] } }
curves = [0, 1, 15, 40, 100]
fine = [[0.5, 1.5, 3.0], [2.0, 1.5, 3.0], [2.4, 2.0, 3.0], [3.0, 1.5, 3.0], [6.0, 2.0, 3.0]]

c_src = <<~C
  #include <stdio.h>
  #include "crt_warp.h"
  int main(void) {
    double x, y;
    int curve;
    char kind;
    double a, b, c;
    while (scanf(" %c", &kind) == 1) {
      if (kind == 'f') {
        if (scanf("%lf %lf %lf", &a, &b, &c) != 3) return 1;
        printf("%.17g\\n", crt_fine_weight(a, b, c));
        continue;
      }
      if (scanf("%d %lf %lf", &curve, &x, &y) != 3) return 1;
      double u, v, s, t;
      crt_warp_to_source(x, y, curve, &u, &v);
      crt_warp_to_display(x, y, curve, &s, &t);
      printf("%.17g %.17g %.17g %.17g %d %.17g\\n", u, v, s, t, crt_inside(u, v), crt_curve_coef(curve));
    }
    return 0;
  }
C

js_src = <<~JS
  const crt = require(#{File.join(core, 'wasm/web/crt.js').to_json});
  const lines = require('fs').readFileSync(0, 'utf8').trim().split('\\n');
  const out = [];
  for (const l of lines) {
    const f = l.trim().split(/\\s+/);
    if (f[0] === 'f') { out.push(String(crt.fineWeight(+f[1], +f[2], +f[3]))); continue; }
    const c = +f[1], x = +f[2], y = +f[3];
    const s = crt.warpToSource(x, y, c), d = crt.warpToDisplay(x, y, c);
    out.push([s[0], s[1], d[0], d[1], crt.inside(s) ? 1 : 0, Math.max(0, Math.min(100, c)) / 100 * crt.CURVE_MAX].join(' '));
  }
  console.log(out.join('\\n'));
JS

input = +""
curves.each { |c| points.each { |x, y| input << "w #{c} #{format('%.17g', x)} #{format('%.17g', y)}\n" } }
fine.each { |a, b, c| input << "f #{a} #{b} #{c}\n" }

failures = 0
worst = 0.0
Dir.mktmpdir("warp_c") do |dir|
  File.write(File.join(dir, "t.c"), c_src)
  File.write(File.join(dir, "t.js"), js_src)
  bin = File.join(dir, "t")
  out, st = Open3.capture2e("gcc", "-std=c11", "-O2", "-Wall", "-Werror", "-I", crt_dir, "-o", bin,
                            File.join(dir, "t.c"), File.join(crt_dir, "crt_warp.c"), "-lm")
  abort "warp_c_test: gcc failed:\n#{out}" unless st.success?
  c_out, st = Open3.capture2(bin, stdin_data: input)
  abort "warp_c_test: the C program failed" unless st.success?
  j_out, st = Open3.capture2("node", File.join(dir, "t.js"), stdin_data: input)
  abort "warp_c_test: node failed" unless st.success?
  c_lines = c_out.lines
  j_lines = j_out.lines
  abort "warp_c_test: #{c_lines.size} C lines, #{j_lines.size} JS lines" unless c_lines.size == j_lines.size
  input.lines.each_with_index do |q, i|
    cv = c_lines[i].split.map(&:to_f)
    jv = j_lines[i].split.map(&:to_f)
    bad = cv.size != jv.size
    cv.zip(jv).each do |a, b|
      d = (a - b).abs
      worst = d if d > worst
      bad ||= d > 1e-12
    end
    next unless bad
    failures += 1
    puts "FAIL #{q.strip}: C #{c_lines[i].strip} / JS #{j_lines[i].strip}" if failures <= 10
  end
end

n = input.lines.size
if failures.zero?
  puts "warp_c_test: #{n} points, C = JS (largest difference #{worst})"
else
  abort "warp_c_test: #{failures} of #{n} points differ"
end
