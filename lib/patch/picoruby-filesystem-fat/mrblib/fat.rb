require "time"

class FAT
  class VFSMethods
  end

  AM_RDO = 0b000001 # Read only
  AM_HID = 0b000010 # Hidden
  AM_SYS = 0b000100 # System
  #AM_?? = 0b001000 # ???????
  AM_DIR = 0b010000 # Directory
  AM_ARC = 0b100000 # Archive

  class Stat

    LABEL = "ADSHR size   datetime                  name"

    def initialize(prefix, path)
      @stat_hash = if path == "/"
        { mode: AM_DIR }
      else
        _stat("#{prefix}#{path}")
      end
    end

    def mode
      @stat_hash[:mode]
    end

    def mode_str
      (0 < mode & AM_ARC ? "A" : "-") <<
      (0 < mode & AM_DIR ? "D" : "-") <<
      (0 < mode & AM_SYS ? "S" : "-") <<
      (0 < mode & AM_HID ? "H" : "-") <<
      (0 < mode & AM_RDO ? "R" : "-")
    end

    def writable?
      (mode & AM_RDO) == 0
    end

    def mtime
      @mtime ||= Time.at(@stat_hash[:unixtime] || 0)
    end

    def birthtime
      raise "NotImplementedError"
    end

    def size
      @stat_hash[:size] || 0
    end

    def directory?
      0 < mode & AM_DIR
    end
  end

  class Dir
    # Dummy method to register Dir instance method symbols in presym
    def self._dummy_for_presym
      dir = FAT::Dir.new("/")
      dir.findnext
      dir.pat = ""
      dir.rewind
    end
  end

  class File
    # Dummy method to register File instance method symbols in presym
    def self._dummy_for_presym
      file = FAT::File.new("/", "r")
      file.expand(0)
      file.fsync
    end
  end

  # device can be "0".."9", :ram, :flash, etc
  # The name is case-insensitive
  def initialize(device, label: "PICORUBY", driver: nil)
    @prefix = "#{device}:"
    @label = label
    case driver
    when nil
      # Do nothing
    when SPI
      FAT.init_spi(
        driver.unit,
        driver.sck_pin,
        driver.cipo_pin,
        driver.copi_pin,
        driver.cs_pin
      )
      sleep_ms 10
    end
  end

  attr_reader :mountpoint, :prefix

  def mkfs
    self._mkfs(@prefix)
    self.setlabel
    self
  end

  def setlabel
    return 0 unless @label
    self._setlabel("#{@prefix}#{@label}")
  end

  def getlabel
    self._getlabel(@prefix)
  end

  def sector_count
    res = self.getfree(@prefix)
    {total: (res >> 16), free: (res & 0b1111111111111111) }
  end

  def mount(mountpoint)
    @mountpoint = mountpoint
    @fatfs = self._mount("#{@prefix}#{mountpoint}")
  end

  def unmount
    self._unmount(@prefix)
    @fatfs = nil
  end

  def open_dir(path)
    FAT::Dir.new("#{@prefix}#{path}")
  end

  def open_file(path, mode)
    FAT::File.new("#{@prefix}#{path}", mode)
  end

  def chdir(path)
    # FatFs where FF_STR_VOLUME_ID == 2 configured
    # calls f_chdrive internally in f_chdir.
    # This is the reason of passing also @prefix
    path = "" if path == "/"
    if path == "" || _exist?("#{@prefix}#{path}")
      _chdir("#{@prefix}#{path}")
    else
      0
    end
  end

  def erase
    _erase(@prefix)
  end

  def utime(atime, _mtime, path)
    _utime(atime.to_i, "#{@prefix}#{path}")
  end

  def mkdir(path, mode = AM_DIR)
    _mkdir("#{@prefix}#{path}", mode)
  end

  def chmod(mode, path)
    _chmod(mode, "#{@prefix}#{path}")
  end

  def exist?(path)
    _exist?("#{@prefix}#{path}")
  end

  def unlink(path)
    _unlink("#{@prefix}#{path}")
  end

  def rename(from, to)
    _rename("#{@prefix}#{from}", "#{@prefix}#{to}")
  end

  def directory?(path)
    return true if path == "/"
    _directory?("#{@prefix}#{path}")
  end

  def contiguous?(path)
    _contiguous?("#{@prefix}#{path}")
  end

  # Dummy method to register symbols in presym
  # These method names are used in C code (fat.c, fat_file.c, fat_dir.c) but not in Ruby code,
  # so we need to reference them here to ensure presym generates the symbol IDs.
  def self._dummy_for_presym
    # Class name
    FAT

    # Hash keys used in mrb__stat (fat.c:274)
    { unixtime: 0 }

    # Method symbols from fat_file.c
    vfs_methods

    # Method symbols from fat.c
    self.unixtime_offset = 0
    _mkdir("", 0)
    _unlink("")
    _rename("", "")
    _exist?("")
    _directory?("")
    _erase("")
    _mkfs("")
    getfree("")
    _mount("")
    _unmount("")
    _utime(0, "")
    _chmod(0, "")
    _setlabel("")
    _getlabel("")
    _contiguous?("")
    _stat("")

    # Method symbols from fat_dir.c and fat_file.c
    # Note: These are registered in the respective class _dummy_for_presym methods
    FAT::Dir._dummy_for_presym
    FAT::File._dummy_for_presym
  end
end
