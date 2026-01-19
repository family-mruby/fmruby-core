# Family mruby OS - Logging Module
# Provides ESP-IDF style logging for Ruby code
#
# Usage:
#   Log.i("TAG", "Info message")
#   Log.e("TAG", "Error message")
#   Log.set_level(Log::LEVEL_DEBUG)

module Log
  # Convenience methods with default tag
  class << self
    # Log with automatic tag based on caller
    def log_with_level(level, message, tag = nil)
      tag ||= "APP"
      case level
      when LEVEL_ERROR
        e(tag, message)
      when LEVEL_WARN
        w(tag, message)
      when LEVEL_INFO
        i(tag, message)
      when LEVEL_DEBUG
        d(tag, message)
      when LEVEL_VERBOSE
        v(tag, message)
      end
    end
    
    # Shorthand methods
    def error(message, tag = "APP")
      e(tag, message.to_s)
    end
    
    def warn(message, tag = "APP")
      w(tag, message.to_s)
    end
    
    def info(message, tag = "APP")
      i(tag, message.to_s)
    end
    
    def debug(message, tag = "APP")
      d(tag, message.to_s)
    end
    
    def verbose(message, tag = "APP")
      v(tag, message.to_s)
    end
  end
end
