# Runtime exception examples

# 1. NoMethodError
puts "Testing NoMethodError..."
nil.no_such_method

# 2. ZeroDivisionError (won't reach here)
puts "Testing ZeroDivisionError..."
1 / 0

# 3. NameError
puts "Testing NameError..."
undefined_variable
