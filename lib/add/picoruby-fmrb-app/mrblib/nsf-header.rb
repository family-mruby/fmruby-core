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
    return nil unless buf && buf.length >= 128

    # Validate magic: "NESM\x1A"
    return nil unless buf[0] == 'N' && buf[1] == 'E' && buf[2] == 'S' &&
                      buf[3] == 'M' && buf.getbyte(4) == 0x1A

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
