# The "algorithm" of the minimal sample. Spinel compiles this file to native
# code and runs it; an mruby app reaches it through the gem. Kept trivial on
# purpose -- this gem exists to show the plumbing, not to compute anything.
class SpinelHelloCore
  def greet
    "Hello Spinel"
  end
end
