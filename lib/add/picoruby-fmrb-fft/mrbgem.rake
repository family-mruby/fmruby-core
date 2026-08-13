MRuby::Gem::Specification.new('picoruby-fmrb-fft') do |spec|
  spec.license = 'MIT'
  spec.author  = ['Katsuhiko Kageyama']
  spec.summary = 'One FFT, four engines: Ruby, Spinel, C and esp-dsp'

  # No dependency line for Math: mruby-math is in every target's build already
  # (p5.rb leans on it the same way).
end
