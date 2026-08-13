# Fmrb::Fft -- one FFT, four engines, chosen at run time.
#
#   fft = Fmrb::Fft.new(size: 512, backend: :spinel)
#   mag = fft.forward(samples)                 # int16 bytes in, int16 bytes out
#   r   = Fmrb::Fft.bench(size: 512, iters: 100, backend: :c)
#   #=> { backend: :c, us_avg: 41.2, us_min: 40.0, iters: 100, reps: 5, mag: "..." }
#
# The four backends (doc/mic_spectrum/plan.md):
#
#   :ruby    fft_core.rb on the mruby VM
#   :spinel  the same fft_core.rb, compiled to native code by Spinel
#   :c       main/kernel/fmrb_fft_bench.c -- the plain baseline
#   :dsp     esp-dsp's assembler radix-2, the ceiling (device builds only)
#
# The interface is the same for all four: samples and magnitudes cross as
# little-endian int16 byte Strings, and every backend runs its repetitions
# inside its own engine, so what is timed is the transform rather than the
# call.
module Fmrb
  class Fft
    BACKENDS = [:ruby, :c, :dsp, :spinel]

    # Repetitions of the timed run. avg comes from all of them, min from the
    # best -- min is the engine at its cleanest, avg includes whatever the
    # engine does between transforms (on mruby, that is the GC).
    DEFAULT_REPS = 5

    attr_reader :size, :backend

    def initialize(size: 512, backend: :ruby)
      unless BACKENDS.include?(backend)
        raise ArgumentError, "unknown FFT backend: #{backend}"
      end
      unless size >= 64 && size <= 1024 && (size & (size - 1)) == 0
        raise ArgumentError, "FFT size must be a power of two in 64..1024: #{size}"
      end
      @size = size
      @backend = backend
      @core = ::FftCore.new(size) if backend == :ruby
      Fmrb::Fft.spinel_open(size) if backend == :spinel
    end

    def self.available?(backend)
      case backend
      when :ruby then true
      when :c then true
      when :dsp then ::FftNative.dsp_available?
      when :spinel then ::FftNative.spinel_available?
      else false
      end
    end

    # Microseconds from the same monotonic clock every backend is timed with.
    def self.micros
      ::FftNative.micros
    end

    # One transform. Returns size/2 little-endian int16 magnitudes.
    def forward(samples)
      run(samples, 1)[1]
    end

    # `iters` transforms of the same input, timed inside the engine.
    # Returns [microseconds, magnitude bytes].
    def run(samples, iters)
      case @backend
      when :ruby
        @core.load(samples)
        t0 = ::FftNative.micros
        @core.run(iters)
        us = ::FftNative.micros - t0
        [us, @core.magnitudes_bytes]
      when :c
        ::FftNative.c_run(samples, @size, iters)
      when :dsp
        ::FftNative.dsp_run(samples, @size, iters)
      when :spinel
        ::FftNative.spinel_run(samples, @size, iters)
      else
        [0, ""]
      end
    end

    # A synthetic input every backend can be fed: `freq` cycles per `size`
    # samples of a sine at `amp`, as int16 bytes. Kept here so the four
    # engines are compared on the same waveform without a file.
    def self.sine(size: 512, cycles: 8, amp: 12000)
      pi = 3.141592653589793
      out = "\x00" * (size * 2)
      i = 0
      while i < size
        v = (amp * Math.sin(2.0 * pi * cycles * i / size)).to_i
        v = 32767 if v > 32767
        v = -32768 if v < -32768
        v += 65536 if v < 0
        out.setbyte(i * 2, v & 0xFF)
        out.setbyte(i * 2 + 1, (v >> 8) & 0xFF)
        i += 1
      end
      out
    end

    # Read one magnitude bin out of what forward/run returned.
    def self.bin(mag, index)
      lo = mag.getbyte(index * 2)
      hi = mag.getbyte(index * 2 + 1)
      return 0 if lo.nil? || hi.nil?
      lo | (hi << 8)
    end

    # Index of the loudest bin -- the cheap correctness check: it has to land
    # on the frequency that was put in, for every backend.
    def self.peak_bin(mag)
      best = 0
      best_v = -1
      i = 0
      count = mag.bytesize / 2
      while i < count
        v = bin(mag, i)
        if v > best_v
          best_v = v
          best = i
        end
        i += 1
      end
      best
    end

    # Time one backend. `samples` defaults to the shared sine above.
    def self.bench(size: 512, iters: 100, backend: :c, reps: DEFAULT_REPS, samples: nil)
      samples ||= sine(size: size)
      fft = new(size: size, backend: backend)
      total = 0
      best = nil
      mag = ""
      r = 0
      while r < reps
        us, mag = fft.run(samples, iters)
        total += us
        best = us if best.nil? || us < best
        r += 1
      end
      fft.close
      {
        backend: backend,
        size: size,
        iters: iters,
        reps: reps,
        us_avg: total.to_f / (reps * iters),
        us_min: best.to_f / iters,
        mag: mag,
        peak_bin: peak_bin(mag),
      }
    end

    # The Spinel backend keeps a runtime instance alive between calls; hand it
    # back when the object is done with. Harmless for the other three.
    def close
      Fmrb::Fft.spinel_close if @backend == :spinel
      nil
    end

    # One Spinel instance per task is enough, and creating it costs a memory
    # pool -- so it is opened on demand and reference counted rather than
    # tied to one Fft object.
    #
    # Constraint: the :spinel backend is a single instance owned by one task
    # (the native side holds it in file-scope statics, current on the task that
    # opened it). Use :spinel from one task only; from another task use :c/:dsp.
    def self.spinel_open(size)
      @spinel_refs ||= 0
      if @spinel_refs == 0
        raise RuntimeError, "the Spinel FFT backend is not in this build" unless ::FftNative.spinel_available?
        rc = ::FftNative.spinel_begin(size)
        raise RuntimeError, "could not start the Spinel FFT instance (#{rc})" if rc < 0
      end
      @spinel_refs += 1
    end

    def self.spinel_close
      @spinel_refs ||= 0
      return if @spinel_refs == 0
      @spinel_refs -= 1
      ::FftNative.spinel_end if @spinel_refs == 0
    end
  end
end
