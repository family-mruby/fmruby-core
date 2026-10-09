# The case/in reproduction of doc/ruby_asterism/upstream/case_in_repro.rb,
# compiled as an app (not through eval) so the result is the app loader's
# own compile. Results go to the log as "CASEIN ok|NG ...".
def caseinr_check(label, got, want)
  Log.info("CASEIN #{got == want ? 'ok' : 'NG'} #{label}: got #{got.inspect}, want #{want.inspect}")
end

def caseinr_literal_values
  out = []
  [{x: 3}, {x: 4}].each do |h|
    out << case h
           in {x: 3} then :three
           else :no
           end
  end
  out
end

def caseinr_bind_in_block
  pre = nil
  [{a: 1}].each do |m|
    case m
    in {a: pre} then nil
    end
  end
  pre
end

def caseinr_rightward_in_block
  a = nil
  [[1]].each { |x| x => [a] }
  a
end

class CaseInReproApp < FmrbApp
  def on_create
    r = case {x: 1}
        in {x: Integer} then :int
        else :no
        end
    caseinr_check("in {x: Integer}", r, :int)
    r = case {x: 1}
        in {x: 0..2} then :range
        else :no
        end
    caseinr_check("in {x: 0..2}", r, :range)
    caseinr_check("[{x: 3}, {x: 4}] against in {x: 3}", caseinr_literal_values, [:three, :no])
    caseinr_check("({x: 7} in {x: 8})", ({x: 7} in {x: 8}), false)
    caseinr_check("in {a: pre} inside a block", caseinr_bind_in_block, 1)
    caseinr_check("x => [a] inside a block", caseinr_rightward_in_block, 1)
    Log.info("CASEIN DONE")
  end

  def on_update
    1000
  end
end

begin
  app = CaseInReproApp.new
  app.start
rescue => e
  Log.error("CaseInReproApp: #{e}")
end
