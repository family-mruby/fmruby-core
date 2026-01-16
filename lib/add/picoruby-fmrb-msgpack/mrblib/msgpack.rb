# MessagePack serialization module for FmRuby
#
# Provides convenient methods for serializing/deserializing Ruby objects
# to/from MessagePack binary format.
#
# Usage:
#   # Serialize to binary
#   data = MessagePack.pack({key: "value", num: 42})
#
#   # Deserialize from binary
#   obj = MessagePack.unpack(data)
#
# Supported types: Integer, Float, String, Symbol, Array, Hash, true/false/nil

module MessagePack
  # Convenience method: dump is alias for pack
  def self.dump(obj)
    pack(obj)
  end

  # Convenience method: load is alias for unpack
  def self.load(binary)
    unpack(binary)
  end
end
