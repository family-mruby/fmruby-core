# NSF (Nintendo Sound Format) header parser
# Parses the 128-byte header to extract metadata.

class NsfHeader
  attr_reader :song_name, :artist, :copyright, :total_songs, :starting_song

  def initialize(song_name, artist, copyright, total_songs, starting_song)
    @song_name = song_name
    @artist = artist
    @copyright = copyright
    @total_songs = total_songs
    @starting_song = starting_song
  end

  # Parse NSF header from file path.
  # Returns NsfHeader instance or nil on failure.
  def self.parse(path)
    f = File.open(path, "r")
    buf = f.read(128)
    f.close
    # bytesize, NOT length: under UTF-8 strings, length counts characters, and
    # a header whose binary fields happen to form a valid multi-byte sequence
    # (e.g. the load/init/play addresses) counts as < 128 "characters" even
    # though all 128 bytes are there -- which made valid NSFs show no Play
    # button. Byte-index everything below for the same reason.
    return nil unless buf && buf.bytesize >= 128

    # Validate magic: "NESM\x1A"
    return nil unless buf.getbyte(0) == 0x4E && buf.getbyte(1) == 0x45 &&
                      buf.getbyte(2) == 0x53 && buf.getbyte(3) == 0x4D &&
                      buf.getbyte(4) == 0x1A

    total_songs = buf.getbyte(6)
    starting_song = buf.getbyte(7)
    song_name = extract_string(buf, 14, 32)
    artist = extract_string(buf, 46, 32)
    copyright = extract_string(buf, 78, 32)

    NsfHeader.new(song_name, artist, copyright, total_songs, starting_song)
  rescue => e
    nil
  end

  # Extract null-terminated string from buffer
  def self.extract_string(buf, offset, max_len)
    s = ""
    max_len.times do |i|
      c = buf.getbyte(offset + i)
      break if c == 0
      s += c.chr
    end
    s
  end
end
