# The "algorithm" of the minimal sample. Spinel compiles this file to native
# code and runs it; an mruby app reaches it through the gem. Kept trivial on
# purpose -- this gem exists to show the plumbing, not to compute anything.
# It takes a name and returns a greeting, so the sample shows both a value
# crossing IN (the name) and one crossing OUT (the greeting).
class SpinelHelloCore
  def greet(name)
    "Hello #{name}!"
  end
end
