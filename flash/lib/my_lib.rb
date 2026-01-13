module Kernel
    def inspect_env
        puts "=== Classes ==="
        Object.constants.select { |c| 
        begin
            Object.const_get(c).is_a?(Class)
        rescue
            false
        end
        }.sort.each { |c| puts "  #{c}" }

        puts "\n=== Modules ==="
        Object.constants.select { |c| 
        begin
            Object.const_get(c).is_a?(Module) && !Object.const_get(c).is_a?(Class)
        rescue
            false
        end
        }.sort.each { |m| puts "  #{m}" }

        puts "\n=== Global Variables ==="
        global_variables.sort.each do |var|
        begin
            value = eval(var.to_s)
            puts "  #{var} = #{value.inspect}"
        rescue => e
            puts "  #{var} = (error: #{e.message})"
        end
        end

        puts "\n=== Special Constants ==="
        puts "  RUBY_VERSION = #{RUBY_VERSION}"
        puts "  RUBY_ENGINE = #{RUBY_ENGINE}"
        puts "  MRUBY_VERSION = #{MRUBY_VERSION}" if defined?(MRUBY_VERSION)

        puts "\n=== Load Path ==="
        $LOAD_PATH.each { |path| puts "  #{path}" } if defined?($LOAD_PATH)

        puts "\n=== Loaded Features ==="
        $LOADED_FEATURES.each { |feature| puts "  #{feature}" } if defined?($LOADED_FEATURES)
    end
end
