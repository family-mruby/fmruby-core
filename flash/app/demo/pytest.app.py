# Phase 2 check for the MicroPython guest VM.
#
# There are no graphics bindings yet, so this only computes and prints. The
# long loop in the middle exists so there is something to interrupt: a stop
# request from the kernel has to unwind it, which is what exercises the VM
# hook. Sized to run for a few seconds on the Linux simulation, which is fast
# enough to leave a window for injecting the stop and short enough that
# start/stop can be repeated without waiting around. Replaced by a real demo
# in phase 3.

print("pytest: start")
print("pytest: squares", sum(x * x for x in range(100)))
print("pytest: chars", [c for c in "fmrb"])
print("pytest: float", 1.0 / 8)

print("pytest: looping")
total = 0
for chunk in range(80):
    i = 0
    while i < 1000000:
        total += i
        i += 1
    if chunk % 10 == 0:
        print("pytest: chunk", chunk)
print("pytest: loop done", total)

print("pytest: end")
