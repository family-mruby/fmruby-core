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

        # puts "\n=== Global Variables ==="
        # global_variables.sort.each do |var|
        #     begin
        #         value = eval(var.to_s)
        #         puts "  #{var} = #{value.inspect}"
        #     rescue => e
        #         puts "  #{var} = (error: #{e.message})"
        #     end
        # end

        puts "\n=== Special Constants ==="
        puts "  RUBY_VERSION = #{RUBY_VERSION}"
        puts "  RUBY_ENGINE = #{RUBY_ENGINE}"
        begin
            puts "  MRUBY_VERSION = #{MRUBY_VERSION}"
        rescue NameError
            # MRUBY_VERSION not defined
        end

        puts "\n=== Load Path ==="
        begin
            $LOAD_PATH.each { |path| puts "  #{path}" }
        rescue NameError
            # $LOAD_PATH not defined
        end

        puts "\n=== Loaded Features ==="
        begin
            $LOADED_FEATURES.each { |feature| puts "  #{feature}" }
        rescue NameError
            # $LOADED_FEATURES not defined
        end
    end
end
