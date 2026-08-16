#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Pack an fmrb_map JSON document into the .map.bin form TileMap reads on the
# device.
#
# The JSON stays the editable source (generate_world.rb writes it); this makes
# the copy the firmware loads. It matters because a 64x64 map is 4096 tile ids,
# and as JSON those have to become 4096 Integer objects inside 64 Arrays before
# the app can draw anything -- 39 s on a Tab5. Packed, the tiles stay in the
# String the file was read into and a tile costs one getbyte.
#
# Usage:
#   ruby tool/map_json2bin.rb <in.map.json> [out.map.bin]
#
# Layout is documented at the top of TileMap
# (lib/add/picoruby-fmrb-app/mrblib/fmrb-tilemap.rb); keep the two in step.

require "json"

MAGIC   = "FMAP"
VERSION = 1
EMPTY   = 0xFF

def die(msg)
  warn "map_json2bin: #{msg}"
  exit 1
end

in_path = ARGV[0] or die("usage: map_json2bin.rb <in.map.json> [out.map.bin]")
out_path = ARGV[1] || in_path.sub(/\.json\z/, "") + ".bin"

obj = JSON.parse(File.read(in_path))
die("unexpected format: #{obj["format"]}") if obj["format"] != "fmrb_map"
die("unsupported version: #{obj["version"]}") if obj["version"] != VERSION

width  = obj["width"].to_i
height = obj["height"].to_i
layers = obj["layers"] || []
events = obj["events"] || []
walkable = obj["walkable_tiles"] || []
sheet_path = obj["tilesheet"].to_s
spawn = obj["spawn"] || {}

die("width/height out of range") if width <= 0 || height <= 0 || width > 65535 || height > 65535
die("too many layers: #{layers.size}") if layers.size > 255
die("too many events: #{events.size}") if events.size > 255
die("tilesheet path too long") if sheet_path.bytesize > 255
die("too many walkable ids") if walkable.size > 255

out = +"".b
out << MAGIC.b
out << [VERSION].pack("C")
out << [obj["tile_size"].to_i].pack("C")
out << [width, height].pack("v2")
out << [layers.size, obj["tilesheet_cols"].to_i].pack("C2")
out << [spawn["x"].to_i, spawn["y"].to_i].pack("v2")
out << [walkable.size, sheet_path.bytesize, events.size, 0].pack("C4")
out << sheet_path.b
walkable.each do |t|
  die("walkable id out of byte range: #{t}") unless t.is_a?(Integer) && t >= 0 && t < EMPTY
  out << [t].pack("C")
end

layers.each_with_index do |layer, li|
  data = layer["data"]
  die("layer #{li} has no data array") unless data.is_a?(Array) && data.size == height
  data.each_with_index do |row, y|
    die("layer #{li} row #{y} is #{row.class} of #{row.respond_to?(:size) ? row.size : "?"}") unless
      row.is_a?(Array) && row.size == width
    row.each do |t|
      if t.nil?
        out << [EMPTY].pack("C")
      else
        die("tile id out of byte range: #{t}") unless t.is_a?(Integer) && t >= 0 && t < EMPTY
        out << [t].pack("C")
      end
    end
  end
end

events.each do |ev|
  name = ((ev["data"] || {})["name"] || "").to_s
  die("event name too long: #{name}") if name.bytesize > 255
  out << [ev["x"].to_i, ev["y"].to_i, ev["id"].to_i].pack("v3")
  out << [name.bytesize].pack("C")
  out << name.b
end

File.binwrite(out_path, out)
puts "map_json2bin: #{in_path} (#{File.size(in_path)} bytes) -> " \
     "#{out_path} (#{out.bytesize} bytes), #{width}x#{height}, " \
     "#{layers.size} layer(s), #{events.size} event(s)"
