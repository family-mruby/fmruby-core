# Fmrb::Fft -- one FFT, four engines, chosen at run time.
#
#   fft = Fmrb::Fft.new(size: 512, backend: :spinel)
#   mag = fft.forward(samples)                 # int16 bytes in, int16 bytes out
#   r   = Fmrb::Fft.bench(size: 512, iters: 100, backend: :c)
#   #=> { backend: :c, us_avg: 41.2, us_min: 40.0, iters: 100, reps: 5, mag: "..." }
#
# The backends (doc/mic_spectrum/plan.md):
#
#   :ruby    fft_core.rb on the mruby VM
#   :spinel  the same fft_core.rb, compiled to native code by Spinel
#   :c       main/kernel/fmrb_fft_bench.c -- the plain baseline
#   :dsp     esp-dsp's assembler radix-2, the ceiling (device builds only)
#   :c64     the plain baseline again, in double
#
# #backend is the engine that runs, which can differ from the one asked for:
# the Spinel instance is one for the whole machine, owned by the first app
# that opened it, and a second app asking for :spinel / :spinel_q15 gets
# :ruby / :ruby_q15 (SPINEL_FALLBACK).
#
# :c64 is not an engine, it is a control. The two Ruby engines compute in
# mrb_float, which is a double, while :c and :dsp are float32; on a chip whose
# FPU is single precision only, that gap is a software emulation of double
# rather than a rounding difference. :c64 prices it, so :spinel against :c64 is
# the engine overhead alone (doc/mic_spectrum/impl_plan_spinel_perf.md, E1).
#
# Then the same three again with no floating point at all, from
# fft_core_q15.rb / fmrb_fft_c_q15():
#
#   :ruby_q15  :spinel_q15  :c_q15
#
# Fixed point is what an embedded person reaches for on a chip like this, and
# it is where an AOT-compiled Ruby has the most to gain: integers are machine
# integers on Spinel, so the soft-float tax simply is not levied (E4). Their
# magnitudes agree with the float backends to within a few counts rather than
# exactly -- the per-stage shift costs a bit each stage.
#
# The interface is the same for all of them: samples and magnitudes cross as
# little-endian int16 byte Strings, and every backend runs its repetitions
# inside its own engine, so what is timed is the transform rather than the
# call.
module Fmrb
  class Fft
    BACKENDS = [:ruby, :c, :dsp, :spinel, :c64,
                :ruby_q15, :c_q15, :spinel_q15]

    # The ones that compute in fixed point. Their magnitudes are a few counts
    # off the float ones by construction, so a caller checking agreement has to
    # know which family a result came from.
    Q15_BACKENDS = [:ruby_q15, :c_q15, :spinel_q15]

    # What a Spinel backend becomes when another app owns the Spinel
    # instance: the same core (fft_core.rb / fft_core_q15.rb) on mruby. Not
    # :c / :c_q15, which would be faster: the C backends keep their work
    # buffers in file-scope statics shared by every task, so two apps on them
    # at once overwrite each other's transform -- exactly the situation a
    # fallback is for. The Ruby cores live in the app's own VM.
    SPINEL_FALLBACK = { spinel: :ruby, spinel_q15: :ruby_q15 }

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
      # The Spinel instance is one for the whole machine and belongs to the
      # first app that opened it. Another app asking for a Spinel backend runs
      # on the same Ruby core on mruby instead -- slower, not refused, same
      # numbers -- and #backend says so.
      if (backend == :spinel || backend == :spinel_q15) && !Fmrb::Fft.spinel_open(size)
        backend = SPINEL_FALLBACK[backend]
        ::FftNative.spinel_note_fallback(backend.to_s)
      end
      @backend = backend
      @core = ::FftCore.new(size) if backend == :ruby
      @core = ::FftCoreQ15.new(size) if backend == :ruby_q15
    end

    def self.available?(backend)
      case backend
      when :ruby, :ruby_q15 then true
      when :c, :c64, :c_q15 then true
      when :dsp then ::FftNative.dsp_available?
      when :spinel, :spinel_q15 then ::FftNative.spinel_available?
      else false
      end
    end

    # Does this backend compute in fixed point?
    def self.q15?(backend)
      Q15_BACKENDS.include?(backend)
    end

    # Microseconds from the same monotonic clock every backend is timed with.
    def self.micros
      ::FftNative.micros
    end

    # What the last :spinel / :spinel_q15 call cost end to end, against the
    # transform time that run() reported.
    #
    # The difference is what crossing into the Spinel program costs around
    # the transform: copying the samples in, decoding them, encoding the
    # magnitudes and copying them out, plus building the window and twiddle
    # tables on the first call for a size (the program keeps them after
    # that). Read this before quoting a per-frame number for a Spinel backend.
    def self.spinel_total_us
      ::FftNative.spinel_total_us
    end

    # One transform. Returns size/2 little-endian int16 magnitudes.
    def forward(samples)
      run(samples, 1)[1]
    end

    # `iters` transforms of the same input, timed inside the engine.
    # Returns [microseconds, magnitude bytes].
    def run(samples, iters)
      case @backend
      when :ruby, :ruby_q15
        @core.load(samples)
        t0 = ::FftNative.micros
        @core.run(iters)
        us = ::FftNative.micros - t0
        [us, @core.magnitudes_bytes]
      when :c
        ::FftNative.c_run(samples, @size, iters)
      when :c64
        ::FftNative.c64_run(samples, @size, iters)
      when :c_q15
        ::FftNative.c_q15_run(samples, @size, iters)
      when :dsp
        ::FftNative.dsp_run(samples, @size, iters)
      when :spinel
        ::FftNative.spinel_run(samples, @size, iters)
      when :spinel_q15
        ::FftNative.spinel_q15_run(samples, @size, iters)
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
        backend: fft.backend,   # the engine that ran, which a fallback can change
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
      if @backend == :spinel || @backend == :spinel_q15
        Fmrb::Fft.spinel_close
      end
      nil
    end

    # One Spinel instance per task is enough, and creating it costs a memory
    # pool -- so it is opened on demand and reference counted rather than
    # tied to one Fft object.
    #
    # The instance is one for the whole machine and is owned by the app task
    # that opened it first, until that app closes it or ends (the native side
    # holds it in file-scope statics, current on that task). Returns true when
    # this app holds it, false when another app does (the caller then runs on
    # SPINEL_FALLBACK). Raises when the instance cannot be built at all.
    def self.spinel_open(size)
      @spinel_refs ||= 0
      if @spinel_refs == 0
        raise RuntimeError, "the Spinel FFT backend is not in this build" unless ::FftNative.spinel_available?
        rc = ::FftNative.spinel_begin(size)
        return false if rc == ::FftNative::SPINEL_BUSY
        raise RuntimeError, "could not start the Spinel FFT instance (#{rc})" if rc < 0
      end
      @spinel_refs += 1
      true
    end

    def self.spinel_close
      @spinel_refs ||= 0
      return if @spinel_refs == 0
      @spinel_refs -= 1
      ::FftNative.spinel_end if @spinel_refs == 0
    end
  end
end
