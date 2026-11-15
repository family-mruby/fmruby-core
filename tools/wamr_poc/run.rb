#!/usr/bin/env ruby
# frozen_string_literal: true

# WAMR proof of concept (doc/wasm_apps, the stage outside the firmware).
#
# Fetches WAMR 2.4.5, builds the three apps (Rust, TinyGo, Zig) and the
# host twice (Linux x86-64 and emscripten/node) from the same sources, runs
# the checks and prints the acceptance table (A1-A7) and the measurements.
#
#   ruby tools/wamr_poc/run.rb            # everything (fetch, build, test, features)
#   ruby tools/wamr_poc/run.rb fetch|build|test|features|boundary|clean
#
# Tools: Rust (wasm32-unknown-unknown) and node from PATH, emscripten from
# EMSDK_ENV (default ~/emsdk/emsdk_env.sh), TinyGo/Go/Zig unpacked under
# FMRB_TOOLCHAINS (default ~/toolchains). Their caches are kept there too.

require 'fileutils'
require 'open3'
require 'shellwords'

ROOT = File.expand_path(__dir__)
BUILD = File.join(ROOT, 'build')
APPS_OUT = File.join(BUILD, 'apps')
WAMR_TAG = 'WAMR-2.4.5'
WAMR_URL = 'https://github.com/bytecodealliance/wasm-micro-runtime.git'
WAMR_DIR = File.join(ROOT, 'vendor', 'wamr-2.4.5')
TOOLCHAINS = File.expand_path(ENV.fetch('FMRB_TOOLCHAINS', '~/toolchains'))
EMSDK_ENV = File.expand_path(ENV.fetch('EMSDK_ENV', '~/emsdk/emsdk_env.sh'))
ZIG_DIR = Dir.glob(File.join(TOOLCHAINS, 'zig-*')).max || File.join(TOOLCHAINS, 'zig')

LANGS = %w[rust tinygo zig].freeze
FRAMES_EXPECTED_DISPATCH = '1,2,87'
WASM_STACK = 16 * 1024

# Feature-set flags. The runtime is built with reference types and extended
# constant expressions on, so these are the compilers' defaults apart from
# the stack size.
RUST_FLAGS = "-C link-arg=-zstack-size=#{WASM_STACK}"
ZIG_FLAGS = %W[-target wasm32-freestanding -fno-entry -rdynamic -O ReleaseSmall --stack #{WASM_STACK}].freeze
TINYGO_FLAGS = %w[-target=wasm-unknown -opt=z -no-debug].freeze

def tool_env
  cache = File.join(TOOLCHAINS, 'cache')
  path = [File.join(TOOLCHAINS, 'go', 'bin'), File.join(TOOLCHAINS, 'tinygo', 'bin'), ZIG_DIR, ENV['PATH']]
  {
    'PATH' => path.join(':'),
    # keep every cache under the toolchains directory
    'XDG_CACHE_HOME' => cache,
    'XDG_CONFIG_HOME' => File.join(cache, 'config'),
    'GOPATH' => File.join(cache, 'gopath'),
    'GOTOOLCHAIN' => 'local',
    'GOTELEMETRY' => 'off',
    'ZIG_GLOBAL_CACHE_DIR' => File.join(cache, 'zig'),
    'ZIG_LOCAL_CACHE_DIR' => File.join(cache, 'zig-local')
  }
end

def sh(cmd, chdir: ROOT, emsdk: false, allow_fail: false, quiet: true)
  cmd = cmd.shelljoin if cmd.is_a?(Array)
  cmd = "source #{EMSDK_ENV.shellescape} >/dev/null 2>&1 && #{cmd}" if emsdk
  out, status = Open3.capture2e(tool_env, 'bash', '-c', cmd, chdir: chdir)
  unless status.success? || allow_fail
    warn out
    abort "failed: #{cmd}"
  end
  puts out unless quiet
  [out, status.success?]
end

def step(title)
  puts "== #{title}"
end

# ---------------------------------------------------------------- fetch

def fetch
  step 'fetch WAMR'
  if File.directory?(File.join(WAMR_DIR, '.git'))
    puts "  #{WAMR_DIR} exists"
  else
    FileUtils.mkdir_p(File.dirname(WAMR_DIR))
    sh(['git', '-c', 'advice.detachedHead=false', 'clone', '-q', '--depth', '1',
        '--branch', WAMR_TAG, WAMR_URL, WAMR_DIR])
  end
  tag = sh(['git', '-C', WAMR_DIR, 'describe', '--tags', '--exact-match']).first.strip
  abort "WAMR checkout is #{tag}, expected #{WAMR_TAG}" unless tag == WAMR_TAG
  puts "  #{tag} #{sh(['git', '-C', WAMR_DIR, 'rev-parse', 'HEAD']).first.strip}"
end

# ---------------------------------------------------------------- versions

def versions
  v = {}
  v['emscripten'] = sh('emcc --version', emsdk: true).first.lines.first.to_s.strip
  v['rust'] = sh('rustc --version').first.strip
  v['tinygo'] = sh('tinygo version', allow_fail: true).first.strip
  v['go'] = sh('go version', allow_fail: true).first.strip
  v['zig'] = sh('zig version', allow_fail: true).first.strip
  v['node'] = sh('node --version').first.strip
  v['gcc'] = sh('gcc --version').first.lines.first.strip
  v['wamr'] = WAMR_TAG
  v
end

def check_tools
  missing = []
  missing << "TinyGo (#{TOOLCHAINS}/tinygo)" unless File.executable?(File.join(TOOLCHAINS, 'tinygo', 'bin', 'tinygo'))
  missing << "Go (#{TOOLCHAINS}/go)" unless File.executable?(File.join(TOOLCHAINS, 'go', 'bin', 'go'))
  missing << "Zig (#{TOOLCHAINS}/zig-*)" unless File.executable?(File.join(ZIG_DIR, 'zig'))
  missing << "emscripten (#{EMSDK_ENV})" unless File.exist?(EMSDK_ENV)
  abort "missing tools: #{missing.join(', ')}" unless missing.empty?
end

# ---------------------------------------------------------------- build

def build_rust(out, rustflags)
  target_dir = File.join(BUILD, 'cargo', File.basename(out, '.wasm'))
  sh("RUSTFLAGS=#{rustflags.shellescape} CARGO_TARGET_DIR=#{target_dir.shellescape} " \
     'cargo build -q --release --target wasm32-unknown-unknown',
     chdir: File.join(ROOT, 'apps', 'rust'))
  FileUtils.cp(File.join(target_dir, 'wasm32-unknown-unknown', 'release', 'poc_app.wasm'), out)
end

def build_zig(out, flags)
  sh(['zig', 'build-exe', 'app.zig', *flags, "-femit-bin=#{out}"], chdir: File.join(ROOT, 'apps', 'zig'))
  FileUtils.rm_f(out.sub(/\.wasm\z/, '.wasm.o'))
end

def build_tinygo(out, flags)
  sh(['tinygo', 'build', *flags, '-o', out, '.'], chdir: File.join(ROOT, 'apps', 'tinygo'))
end

def build_apps
  step 'build apps'
  FileUtils.mkdir_p(APPS_OUT)
  build_rust(app('rust'), RUST_FLAGS)
  build_tinygo(app('tinygo'), TINYGO_FLAGS)
  build_zig(app('zig'), ZIG_FLAGS)
  # TinyGo with the collectors that free memory, for the churn check
  %w[conservative precise].each do |gc|
    build_tinygo(app("tinygo_gc_#{gc}"), TINYGO_FLAGS + ["-gc=#{gc}"])
  end
  LANGS.each { |l| puts format('  %-8s %6d bytes', l, File.size(app(l))) }
end

def build_host(dir, emscripten:, defs: [])
  cfg = ['cmake', '-S', File.join(ROOT, 'host'), '-B', dir, *defs.map { |d| "-D#{d}" }]
  cfg = ['emcmake', *cfg] if emscripten
  sh(cfg, emsdk: emscripten)
  sh(['cmake', '--build', dir, '-j', '8'], emsdk: emscripten)
end

def build_hosts
  step 'build host (linux, emscripten)'
  build_host(File.join(BUILD, 'linux'), emscripten: false)
  build_host(File.join(BUILD, 'emscripten'), emscripten: true)
end

def app(name)
  File.join(APPS_OUT, "#{name}.wasm")
end

# ---------------------------------------------------------------- run

HOSTS = {
  'linux' => -> { [File.join(BUILD, 'linux', 'poc_host')] },
  'emscripten' => -> { ['node', File.join(BUILD, 'emscripten', 'poc_host.js')] }
}.freeze

Result = Struct.new(:kind, :fields)

def parse(out)
  out.lines.filter_map do |line|
    next unless line.start_with?('RESULT ')

    kind, rest = line.strip.sub('RESULT ', '').split(' ', 2)
    fields = {}
    rest.to_s.scan(/(\w+)=("([^"]*)"|\S+)/) { |k, v, q| fields[k] = q || v }
    Result.new(kind, fields)
  end
end

def host_run(build, *args)
  out, ok = sh([*HOSTS.fetch(build).call, *args], allow_fail: true)
  res = parse(out)
  done = res.find { |r| r.kind == 'done' }
  { out: out, ok: ok && done && done.fields['rc'] == '0', results: res }
end

def find(run, kind)
  run[:results].select { |r| r.kind == kind }
end

# ---------------------------------------------------------------- tests

def test
  step 'run checks'
  rows = {}
  info = []
  all_pool = []

  # A1/A2/A5: one app, 100 frames
  runs = {}
  HOSTS.each_key do |b|
    LANGS.each do |l|
      runs[[b, l]] = r = host_run(b, 'run', app(l))
      all_pool.concat(find(r, 'pool'))
    end
  end
  sums = ->(b) { LANGS.to_h { |l| [l, find(runs[[b, l]], 'run').first&.fields] } }
  good = lambda do |f|
    f && f['checksum'] == f['expected'] && f['dispatch'] == FRAMES_EXPECTED_DISPATCH
  end
  lin = sums.call('linux')
  ems = sums.call('emscripten')
  a1 = LANGS.all? { |l| good.call(lin[l]) } && lin.values.map { |f| f['checksum'] }.uniq.size == 1
  rows['A1'] = [a1, 'linux: ' + LANGS.map { |l| "#{l}=#{lin[l]&.fetch('checksum', '-')}" }.join(' ')]
  a2 = LANGS.all? { |l| good.call(ems[l]) && ems[l]['checksum'] == lin[l]&.fetch('checksum') }
  rows['A2'] = [a2, 'emscripten: ' + LANGS.map { |l| "#{l}=#{ems[l]&.fetch('checksum', '-')}" }.join(' ')]

  # A3: source check + every run above went through the raw path only
  host_c = File.read(File.join(ROOT, 'host', 'host.c'))
  cmake = File.read(File.join(ROOT, 'host', 'CMakeLists.txt'))
  raw_only = host_c.include?('wasm_runtime_register_natives_raw(') &&
             !host_c.match?(/wasm_runtime_register_natives\s*\(/) &&
             cmake.include?('poc_no_invoke_native.c') && cmake.include?('EXCLUDE REGEX "invokeNative_"')
  a3 = raw_only && a1 && a2
  rows['A3'] = [a3, 'register_natives_raw only; invokeNative replaced by a trap in both builds']

  # A4: instruction limit
  spin_ok = true
  spin_notes = []
  HOSTS.each_key do |b|
    LANGS.each do |l|
      r = host_run(b, 'spin', app(l))
      s = find(r, 'spin').first&.fields || {}
      rs = find(r, 'spin_resume').first&.fields || {}
      ag = find(r, 'spin_again').first&.fields || {}
      ok = r[:ok] && s['returned'] == 'no' && s['exc'].to_s.include?('instruction limit exceeded')
      spin_ok &&= ok
      spin_notes << "#{b}/#{l}: #{s['exc']} in #{s['elapsed_us']}us"
      info << "resume #{b}/#{l}: resumed=#{rs['resumed']} checksum_ok=#{rs['checksum'] == rs['expected']} " \
              "stack_pointer=#{rs['stack_pointer']} #{rs['before']}->#{rs['after']} " \
              "second_spin=#{ag['exc']}"
    end
  end
  rows['A4'] = [spin_ok, spin_notes.first(1).join + ' (all 6 runs)']

  # A6: two modules and two instances, updated alternately
  multi_ok = true
  pairs = [%w[rust zig], %w[tinygo rust], %w[zig tinygo]]
  multi_notes = []
  HOSTS.each_key do |b|
    pairs.each do |x, y|
      r = host_run(b, 'multi', app(x), app(y))
      all_pool.concat(find(r, 'pool'))
      m = find(r, 'multi_modules').first&.fields || {}
      i = find(r, 'multi_instances').first&.fields || {}
      ok = r[:ok] && m['a_checksum'] == m['expected'] && m['b_checksum'] == m['expected'] &&
           i['inst1_checksum'] == i['expected'] && i['inst2_checksum'] == i['expected']
      multi_ok &&= ok
      multi_notes << "#{b} #{x}+#{y}:#{ok ? 'ok' : 'NG'}"
    end
  end
  rows['A6'] = [multi_ok, multi_notes.join(' ')]

  # A7: per-app memory cap
  limit_ok = true
  limit_notes = []
  HOSTS.each_key do |b|
    LANGS.each do |l|
      other = l == 'rust' ? 'zig' : 'rust'
      r = host_run(b, 'limit', app(l), app(other))
      all_pool.concat(find(r, 'pool'))
      big = find(r, 'limit_grow_big').first&.fields || {}
      oth = find(r, 'limit_other').first&.fields || {}
      ok = r[:ok] && big['refused'] == 'yes' && oth['ok'] == 'yes' && oth['checksum'] == oth['expected']
      limit_ok &&= ok
      how = big['ok'] == 'yes' ? "grow returned #{big['ret']}" : big['exc']
      limit_notes << "#{b}/#{l}: #{how}" if b == 'linux'
      info << "limit #{b}/#{l}: refused by #{how}; A continues=#{oth['a_continues']}"
    end
  end
  rows['A7'] = [limit_ok, limit_notes.join('; ')]

  # A5: fixed pool
  outside = all_pool.map { |p| p.fields['os_malloc_calls'].to_i }.max
  fails = all_pool.map { |p| p.fields['linmem_failures'].to_i }.max
  a5 = outside.zero? && fails.zero? && runs.values.all? { |r| r[:ok] }
  peak = runs.map do |(b, l), r|
    p = find(r, 'pool').find { |x| x.fields['label'] == 'running' }&.fields || {}
    f = find(r, 'run').first&.fields || {}
    "#{b}/#{l}: pool_highmark=#{p['highmark']} linmem_highmark=#{p['linmem_highmark']} pages=#{f['mem_pages']}"
  end
  first = all_pool.first&.fields || {}
  rows['A5'] = [a5, "pool total=#{first['total']} os_malloc_calls(max)=#{outside}; " \
                    'wasm memory from the second fixed region (os_mmap)']

  # measurements
  bench = HOSTS.keys.product(LANGS).map do |b, l|
    f = find(host_run(b, 'bench', app(l)), 'bench').first&.fields || {}
    "#{b}/#{l}: compute(1e6)=#{f['compute_us']}us native_c=#{f['native_c_us']}us " \
      "host->app x100k=#{f['host_to_app_us']}us app->host x100k=#{f['app_to_host_us']}us"
  end
  churn = HOSTS.keys.product(['tinygo', 'tinygo_gc_conservative', 'tinygo_gc_precise']).map do |b, l|
    f = find(host_run(b, 'churn', app(l), '1000', '4096'), 'churn').first&.fields || {}
    "#{b}/#{l}: ok=#{f['ok']} pages #{f['pages_before']}->#{f['pages_after']} #{f['exc'] == '(none)' ? '' : f['exc']}"
  end

  sizes = LANGS.map { |l| "#{l}.wasm=#{File.size(app(l))}" } +
          %w[conservative precise].map { |g| "tinygo_gc_#{g}.wasm=#{File.size(app("tinygo_gc_#{g}"))}" }
  text = ->(f) { sh(['size', f]).first.lines[1].split.first.to_i }
  lin_host = text.call(File.join(BUILD, 'linux', 'poc_host'))
  lin_base = text.call(File.join(BUILD, 'linux', 'baseline'))
  ems_host = File.size(File.join(BUILD, 'emscripten', 'poc_host.wasm'))
  ems_base = File.size(File.join(BUILD, 'emscripten', 'baseline.wasm'))

  puts
  puts '== acceptance'
  %w[A1 A2 A3 A4 A5 A6 A7].each do |k|
    ok, note = rows[k]
    puts format('%-3s %-4s %s', k, ok ? 'PASS' : 'FAIL', note)
  end
  puts
  puts '== resume after a stop / grow refusal'
  info.each { |s| puts "  #{s}" }
  puts '== pool and memory (one app, 100 frames)'
  peak.each { |s| puts "  #{s}" }
  puts '== sizes'
  puts "  #{sizes.join(' ')}"
  puts "  linux text: poc_host=#{lin_host} baseline=#{lin_base} wamr+host=#{lin_host - lin_base}"
  puts "  emscripten wasm: poc_host=#{ems_host} baseline=#{ems_base} wamr+host=#{ems_host - ems_base}"
  puts '== speed (one run each)'
  bench.each { |s| puts "  #{s}" }
  puts '== TinyGo GC churn (1000 x 4096 bytes, dropped)'
  churn.each { |s| puts "  #{s}" }
  puts '== versions'
  versions.each { |k, v| puts "  #{k}: #{v}" }
  rows.values.all?(&:first)
end

# ---------------------------------------------------------------- features

# Which compiler outputs load into a runtime with reference types and
# extended constant expressions off, and which flags make them load.
def features
  step 'feature set (runtime with REF_TYPES=0, EXTENDED_CONST_EXPR=0)'
  dir = File.join(BUILD, 'linux-noref')
  build_host(dir, emscripten: false, defs: %w[WAMR_BUILD_REF_TYPES=0 WAMR_BUILD_EXTENDED_CONST_EXPR=0])
  variants = {
    'rust default' => app('rust'),
    'rust target-cpu=mvp +features' => lambda { |out|
      build_rust(out, "#{RUST_FLAGS} -C target-cpu=mvp " \
                      '-C target-feature=+bulk-memory,+sign-ext,+nontrapping-fptoint,+mutable-globals,+multivalue')
    },
    'zig default (lime1)' => app('zig'),
    'zig -mcpu=lime1-call_indirect_overlong-extended_const' => lambda { |out|
      build_zig(out, ZIG_FLAGS + ['-mcpu=lime1-call_indirect_overlong-extended_const'])
    },
    'tinygo default' => app('tinygo')
  }
  variants.each_with_index do |(name, src), i|
    path = src
    if src.respond_to?(:call)
      path = File.join(APPS_OUT, "feature_#{i}.wasm")
      src.call(path)
    end
    out, = sh([File.join(dir, 'poc_host'), 'run', path], allow_fail: true)
    r = parse(out)
    run = r.find { |x| x.kind == 'run' }
    err = r.find { |x| x.kind == 'error' }
    status = run ? "loads, checksum=#{run.fields['checksum']} dispatch=#{run.fields['dispatch']}" : err&.fields&.fetch('msg', err.fields.to_s)
    puts format('  %-55s %s', name, status)
  end
  # Rust without a stack size: the default 1 MiB shadow stack
  path = File.join(APPS_OUT, 'rust_default_stack.wasm')
  build_rust(path, '')
  out, = sh([File.join(BUILD, 'linux', 'poc_host'), 'run', path], allow_fail: true)
  run = parse(out).find { |x| x.kind == 'run' }
  puts "  rust without -zstack-size: mem_pages=#{run&.fields&.fetch('mem_pages')}"
end

# ---------------------------------------------------------------- boundary

# doc/app_boundary W4a: the glue generated from sig/ (tools/boundary/gen.rb)
# in this host, and the first C app built against the generated SDK header.
SDK_C = File.expand_path('../../sdk/c', ROOT)

def boundary
  step 'boundary (generated glue, C app)'
  sh([RbConfig.ruby, File.expand_path('../boundary/gen.rb', ROOT)])
  FileUtils.mkdir_p(APPS_OUT)
  out = app('c_boundary')
  sh(['zig', 'cc', '-target', 'wasm32-freestanding', '-nostdlib', '-O2',
      "-I#{SDK_C}", '-Wl,--no-entry', "-Wl,-z,stack-size=#{WASM_STACK}",
      File.join(ROOT, 'apps', 'c_boundary', 'app.c'), '-o', out])
  build_hosts
  ok = true
  HOSTS.each_key do |b|
    run = host_run(b, 'boundary', out)
    r = find(run, 'boundary').first
    pass = run[:ok] && r && r.fields['checksum'] == r.fields['expected'] &&
           r.fields['abi'] == r.fields['abi_expected']
    ok &&= pass
    puts format('  %-10s %s %s', b, pass ? 'ok' : 'FAIL',
                r ? r.fields.slice('checksum', 'expected', 'abi', 'abi_expected', 'wasm_size').map { |k, v| "#{k}=#{v}" }.join(' ') : run[:out])
  end
  ok
end

# ---------------------------------------------------------------- main

cmd = ARGV.first || 'all'
case cmd
when 'fetch' then fetch
when 'build'
  check_tools
  build_apps
  build_hosts
when 'test' then exit(test ? 0 : 1)
when 'features' then features
when 'boundary'
  fetch
  check_tools
  exit(boundary ? 0 : 1)
when 'clean' then FileUtils.rm_rf(BUILD)
when 'all'
  fetch
  check_tools
  build_apps
  build_hosts
  ok = test
  features
  exit(ok ? 0 : 1)
else
  abort "usage: #{$PROGRAM_NAME} [all|fetch|build|test|features|boundary|clean]"
end
