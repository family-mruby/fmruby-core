# tools/ckpt_table/ckpt_table.rb - link the checkpoint table maker and run it
# (doc/checkpoint/flash_table.md, Ff).
#
#   ruby ckpt_table.rb --cc CC --work DIR --out TABLE.c [--run RUNNER]
#                      [--ldflag FLAG]... -- OBJECT... ARCHIVE... [-lX]...
#
# The maker (gen_main.c) is the gem ports and the checkpoint glue of one
# configuration, compiled as the firmware compiles them, with that
# configuration's libmruby. They call into the rest of the firmware (the
# graphics, the kernel, the HAL), which is not linked: those functions are
# given here as stand-ins that return 0 -- "a name only". Opening a VM only
# defines classes and methods, so none of them runs for anything that
# matters. The stand-ins are found by linking once with the unresolved
# references allowed and reading the linker's list.
#
# The table is written to TABLE.c only when it changes, so that a build that
# changes nothing the VM sees compiles nothing again.

require 'optparse'
require 'fileutils'
require 'open3'

opt = { ldflags: [] }
parser = OptionParser.new do |o|
  o.on('--cc CC') { |v| opt[:cc] = v }
  o.on('--work DIR') { |v| opt[:work] = v }
  o.on('--out FILE') { |v| opt[:out] = v }
  o.on('--run RUNNER') { |v| opt[:run] = v }
  o.on('--ldflag FLAG') { |v| opt[:ldflags] << v }
end
inputs = parser.parse(ARGV)
abort parser.to_s unless opt[:cc] && opt[:work] && opt[:out] && !inputs.empty?

cc = opt[:cc].split
work = opt[:work]
FileUtils.mkdir_p(work)
emcc = File.basename(cc.last) =~ /\Aemcc/
exe = File.join(work, emcc ? 'ckpt_table_gen.js' : 'ckpt_table_gen')

def run!(cmd)
  out, st = Open3.capture2e(*cmd)
  abort "ckpt_table: failed: #{cmd.join(' ')}\n#{out}" unless st.success?
  out
end

# 1. which functions the maker lacks
allow = emcc ? ['-sERROR_ON_UNDEFINED_SYMBOLS=0', '-sWARN_ON_UNDEFINED_SYMBOLS=1']
             : ['-Wl,--warn-unresolved-symbols']
log = run!(cc + ['-o', exe] + inputs + opt[:ldflags] + allow)
missing = if emcc
            # emcc does not always print its warnings one by one ("warnings in
            # JS library compilation"); the JS it writes names each one
            log.scan(/undefined symbol: ([A-Za-z_][A-Za-z0-9_]*)/).flatten +
              File.read(exe).scan(/missing function: ([A-Za-z_][A-Za-z0-9_]*)/).flatten
          else
            log.scan(/undefined reference to [`']([A-Za-z_][A-Za-z0-9_]*)'/).flatten
          end
missing = missing.uniq.sort

# The wasm types of the functions the module imports from "env": a call
# through a stand-in of another type is a trap in wasm (wasm-ld puts an
# unreachable stub in its place), so there each stand-in takes the type its
# callers use. Read from the first link's module (its type and import
# sections).
def wasm_import_types(path)
  b = File.binread(path)
  pos = 8
  leb = lambda do
    v = 0
    shift = 0
    loop do
      byte = b.getbyte(pos)
      pos += 1
      v |= (byte & 0x7f) << shift
      shift += 7
      break if byte < 0x80
    end
    v
  end
  name = lambda do
    n = leb.call
    s = b.byteslice(pos, n)
    pos += n
    s
  end
  limits = lambda do
    flags = leb.call
    leb.call
    leb.call if flags & 1 == 1
  end
  types = []
  out = {}
  while pos < b.bytesize
    id = b.getbyte(pos)
    pos += 1
    size = leb.call
    body_end = pos + size
    case id
    when 1
      leb.call.times do
        pos += 1 # 0x60
        params = Array.new(leb.call) { b.getbyte(pos).tap { pos += 1 } }
        results = Array.new(leb.call) { b.getbyte(pos).tap { pos += 1 } }
        types << [params, results]
      end
    when 2
      leb.call.times do
        mod = name.call
        field = name.call
        kind = b.getbyte(pos)
        pos += 1
        case kind
        when 0 then t = leb.call
                    out[field] = types[t] if mod == 'env'
        when 1 then pos += 1
                    limits.call
        when 2 then limits.call
        when 3 then pos += 2
        when 4 then pos += 1
                    leb.call
        end
      end
    end
    pos = body_end
  end
  out
end

C_TYPE = { 0x7f => 'int', 0x7e => 'long long', 0x7d => 'float', 0x7c => 'double' }.freeze

# 2. the stand-ins: a name and nothing else
standins = File.join(work, 'standins.c')
src = +"/* Stand-ins for the firmware functions the table maker does not link\n" \
       "   (tools/ckpt_table/ckpt_table.rb). Generated; do not edit. */\n"
wasm_types = emcc ? wasm_import_types(exe.sub(/\.js\z/, '.wasm')) : {}
missing.each do |s|
  params, results = wasm_types[s]
  if params
    ret = results.empty? ? 'void' : C_TYPE.fetch(results.first)
    args = params.empty? ? 'void' : params.each_with_index.map { |t, i| "#{C_TYPE.fetch(t)} a#{i}" }.join(', ')
    body = results.empty? ? '' : ' return 0;'
    src << "#{ret} #{s}(#{args});\n#{ret} #{s}(#{args}) {#{body} }\n"
  else
    src << "long #{s}(void);\nlong #{s}(void) { return 0; }\n"
  end
end
File.write(standins, src)
run!(cc + ['-w', '-c', standins, '-o', "#{standins}.o"])

# 3. the maker itself, nothing left unresolved
run!(cc + ['-o', exe] + inputs + ["#{standins}.o"] + opt[:ldflags])

# 4. the table
tmp = "#{opt[:out]}.new"
FileUtils.mkdir_p(File.dirname(opt[:out]))
runner = opt[:run] ? opt[:run].split : []
run!(runner + [exe, File.expand_path(tmp)])
if File.exist?(opt[:out]) && File.binread(opt[:out]) == File.binread(tmp)
  FileUtils.rm_f(tmp)
  puts "ckpt_table: #{opt[:out]} unchanged (#{missing.size} stand-ins)"
else
  FileUtils.mv(tmp, opt[:out])
  puts "ckpt_table: wrote #{opt[:out]} (#{missing.size} stand-ins)"
end
# the build's output is touched either way, so that make/ninja see it as done
FileUtils.touch(opt[:out])
