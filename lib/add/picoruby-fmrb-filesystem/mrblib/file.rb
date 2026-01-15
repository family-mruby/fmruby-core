# File class for FMRuby
# Path manipulation methods ported from picoruby-posix-io
# File I/O and test methods implemented in C (src/fmrb_filesystem.c)

class File < IO
  # File separator constant
  SEPARATOR = "/"
  ALT_SEPARATOR = nil  # Not Windows

  # File::Constants module is defined in C (src/fmrb_filesystem.c)
  # and includes both FNM_* and file open mode constants

  # Initialize File object
  def initialize(path, mode = "r")
    _open(path, mode)
  end

  # Open a file
  def self.open(path, mode = "r", &block)
    file = new(path, mode)
    if block
      begin
        block.call(file)
      ensure
        file.close unless file.closed?
      end
    else
      file
    end
  end

  # Join path components
  def self.join(*names)
    return "" if names.empty?

    names = names.map do |name|
      case name
      when String
        name
      when Array
        if names == name
          raise ArgumentError, "recursive array"
        end
        join(name)
      else
        raise TypeError, "no implicit conversion of #{name.class} into String"
      end
    end

    return names[0] if names.size == 1

    if names[0][-1] == File::SEPARATOR
      s = names[0][0, names[0].size - 1]
    else
      s = names[0].dup
    end

    s = s.to_s

    (1..names.size-2).each { |i|
      t = names[i]
      if t[0] == File::SEPARATOR && t[-1] == File::SEPARATOR
        t = t[1, t.size - 2]
      elsif t[0] == File::SEPARATOR
        t = t[1, t.size - 1]
      elsif t[-1] == File::SEPARATOR
        t = t[0, t.size - 1]
      end
      t = t.to_s
      s += File::SEPARATOR + t if t != ""
    }
    if names[-1][0] == File::SEPARATOR
      s += File::SEPARATOR + names[-1][1, names[-1].size - 1].to_s
    else
      s += File::SEPARATOR + names[-1]
    end
    s
  end

  # Internal helper for expand_path
  def self._concat_path(path, base_path)
    if path[0] == "/" || path[1] == ':' # Windows root!
      expanded_path = path
    elsif path[0] == "~"
      if (path[1] == "/" || path[1] == nil)
        dir = path[1, path.size]
        home_dir = ENV["HOME"]

        unless home_dir
          raise ArgumentError, "couldn't find HOME environment -- expanding '~'"
        end

        expanded_path = home_dir
        expanded_path += dir if dir
        expanded_path += "/"
      else
        # ~username expansion not supported in embedded environment
        raise ArgumentError, "~username expansion not supported"
      end
    else
      expanded_path = _concat_path(base_path, ENV["PWD"] || "/")
      expanded_path += "/" + path
    end

    expanded_path
  end

  # Expand path to absolute path
  def self.expand_path(path, default_dir = '.')
    expanded_path = _concat_path(path, default_dir)
    drive_prefix = ""
    if File::ALT_SEPARATOR && expanded_path.size > 2 && ("A".."Z")===(expanded_path[0]&.upcase) && expanded_path[1] == ":"
      drive_prefix = expanded_path[0, 2].to_s
      expanded_path = expanded_path[2, expanded_path.size]
    end
    expand_path_array = []
    if File::ALT_SEPARATOR && expanded_path&.include?(File::ALT_SEPARATOR)
      expand_path = expanded_path.gsub(File::ALT_SEPARATOR, '/')
    end
    while expanded_path&.include?('//')
      expanded_path = expanded_path.gsub('//', '/')
    end

    if expanded_path != "/"
      expanded_path&.split('/')&.each do |path_token|
        if path_token == '..'
          if expand_path_array.size > 1
            expand_path_array.pop
          end
        elsif path_token == '.'
          # nothing to do.
        else
          expand_path_array << path_token
        end
      end

      expanded_path = expand_path_array.join("/")
      if expanded_path.empty?
        expanded_path = '/'
      end
    end
    if drive_prefix.empty?
      expanded_path.to_s
    else
      drive_prefix + expanded_path&.gsub("/", File::ALT_SEPARATOR).to_s
    end
  end

  # Extract basename from path
  def self.basename(path, suffix = '')
    raise ArgumentError, "path contains null byte" if path.include?("\0")

    # Remove trailing slashes
    while path.size > 1 && path[-1] == '/'
      path = path[0, path.size - 1]
    end

    # If path is just "/", return "/"
    return '/' if path == '/'

    # Extract basename
    last_slash = path.rindex('/')
    if last_slash
      basename = path[last_slash + 1, path.size]
    else
      basename = path
    end

    # Remove suffix if provided
    if !suffix.empty? && basename.end_with?(suffix) && suffix != basename
      basename = basename[0, basename.size - suffix.size]
    end

    basename
  end

  # Extract dirname from path
  def self.dirname(path, level = 1)
    raise ArgumentError, "negative level: #{level}" if level < 0

    result = path
    level.times do
      # Remove trailing slashes
      while result.size > 1 && result[-1] == '/'
        result = result[0, result.size - 1]
      end

      # If result is just "/", return "/"
      break if result == '/'

      # Find last slash
      last_slash = result.rindex('/')
      if last_slash.nil?
        result = '.'
        break
      elsif last_slash == 0
        result = '/'
        break
      else
        result = result[0, last_slash]
      end
    end

    result
  end

  # Extract file extension
  def self.extname(filename)
    fname = self.basename(filename)
    epos = fname.rindex('.')
    return '' if epos == 0 || epos.nil?
    return fname[epos, fname.size - epos]
  end

  # Get file path (identity function for strings)
  def self.path(filename = nil)
    if self.instance_of?(self.class)
      @path
    else
      raise ArgumentError, "missing filename" if filename.nil?
      if filename.is_a?(String)
        filename
      else
        raise TypeError, "no implicit conversion of #{filename.class} into String"
      end
    end
  end

  # Following methods are implemented in C (src/fmrb_filesystem.c):
  # - File.file?(path)
  # - File.exist?(path)
  # - File.directory?(path)
  # - File.size(path)
end
