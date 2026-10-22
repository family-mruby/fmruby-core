# Host tests for the device's CA bundle (doc/ca_bundle/plan.md).
#
# The committed config/cacrt_fmrb.pem is what the ESP32 build turns into the
# device's whole trust store, so it is checked as it ships: none of the
# excluded roots is in it, its count is the source's minus the excluded ones,
# and every certificate parses. The tool's pure parts (the exclude list, the
# PEM split, the "name not found" stop) run against small inputs.
#
#   ruby test/ca_bundle/run.rb

require "openssl"
require_relative "../../tools/cacrt_bundle"

$failures = 0

def check(label)
  ok = yield
  if ok
    puts "ok   #{label}"
  else
    puts "FAIL #{label}"
    $failures += 1
  end
rescue => e
  puts "FAIL #{label} (#{e.class}: #{e.message})"
  $failures += 1
end

def eq(label, actual, expected)
  check("#{label} (#{actual.inspect})") { actual == expected }
end

def raises(label, klass, pattern)
  yield
  puts "FAIL #{label} (nothing raised)"
  $failures += 1
rescue klass => e
  check("#{label} (#{e.message})") { e.message.match?(pattern) }
end

G3 = "emSign ECC Root CA - G3"

# ---- the tool's pure parts -------------------------------------------------

eq "exclude list: comments and blank lines dropped",
   CacrtBundle.read_exclude("# head\n\nA Root  # why\n  B Root\n#C Root\n"), ["A Root", "B Root"]

# Real certificates from the committed bundle, re-wrapped as a small source.
shipped = CacrtBundle.parse_pem(File.read(CacrtBundle::OUTPUT_FILE))
a, b = shipped[0], shipped[1]
src = "## header\n\n#{a.name}\n#{a.underline}\n#{a.pem}\n#{b.name}\n#{b.underline}\n#{b.pem}\n#{b.pem}"
parsed = CacrtBundle.parse_pem(src)
eq "parse: three certificates", parsed.size, 3
eq "parse: names from the line above the underline", parsed.map(&:name), [a.name, b.name, nil]
check("parse: PEM text kept as is") { parsed[0].pem == a.pem }
eq "exclude: the named one goes", CacrtBundle.exclude(parsed.first(2), [a.name]).map(&:name), [b.name]
raises("exclude: a misspelt name stops", CacrtBundle::Error, /not found/) do
  CacrtBundle.exclude(parsed, ["GlobalSign Root CA "])
end
raises("parse: an unterminated certificate stops", CacrtBundle::Error, /unterminated/) do
  CacrtBundle.parse_pem("#{CacrtBundle::BEGIN_LINE}\nAAAA\n")
end
r1 = CacrtBundle.render(entries: parsed.first(2), idf_version: "v0.0.0", sources: [["x.pem", "0" * 64]],
                        source_count: 3, excluded: ["Z"], mozilla: nil)
r2 = CacrtBundle.render(entries: parsed.first(2), idf_version: "v0.0.0", sources: [["x.pem", "0" * 64]],
                        source_count: 3, excluded: ["Z"], mozilla: nil)
eq "render: same input, same bytes", r1 == r2, true
check("render: the output parses back to the same entries") { CacrtBundle.parse_pem(r1) == parsed.first(2) }

# ---- the committed bundle --------------------------------------------------

text = File.read(CacrtBundle::OUTPUT_FILE)
entries = CacrtBundle.parse_pem(text)
excluded = CacrtBundle.read_exclude(File.read(CacrtBundle::EXCLUDE_FILE))
eq "exclude list names G3", excluded.include?(G3), true

m = text.match(/^## Certificates: (\d+) in the source, (\d+) excluded, (\d+) here$/)
check("bundle header has the counts") { m }
if m
  source, nexcl, here = m.captures.map(&:to_i)
  eq "header: excluded count matches the list", nexcl, excluded.size
  eq "header: count here matches the certificates", here, entries.size
  eq "count = source - excluded", entries.size, source - excluded.size
end
header_names = text[/^## Excluded:\n((?:##   .+\n)*)/, 1].to_s.lines.map { |l| l.sub("##   ", "").chomp }
eq "header lists the excluded names", header_names, excluded

certs = []
check("every certificate parses (#{entries.size})") do
  certs = entries.map { |e| OpenSSL::X509::Certificate.new(e.pem) }
  certs.size == entries.size
end
eq "every certificate has a name", entries.count { |e| e.name.nil? }, 0
excluded.each do |n|
  eq "no #{n.inspect} by name", entries.count { |e| e.name == n }, 0
  eq "no #{n.inspect} by subject CN",
     certs.count { |c| c.subject.to_a.any? { |k, v, _| k == "CN" && v == n } }, 0
end
check("within CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS (200)") { entries.size <= 200 }

puts
if $failures.zero?
  puts "all CA bundle tests passed"
else
  puts "#{$failures} failure(s)"
  exit 1
end
