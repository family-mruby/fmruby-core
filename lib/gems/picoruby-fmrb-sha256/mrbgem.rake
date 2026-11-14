# Digest::SHA256 in C for the builds that have no mbedTLS (the browser
# build): Asterism::Transfer's whole-file check (doc/asterism_file_transfer
# T2). The other builds carry picoruby-mbedtls (MbedTLS::Digest) and do not
# take this gem.
MRuby::Gem::Specification.new('picoruby-fmrb-sha256') do |spec|
  spec.license = 'MIT'
  spec.author  = ['Katsuhiko Kageyama']
  spec.summary = 'SHA-256 (Digest::SHA256: new, update, hexdigest, digest) for Family mruby'
end
