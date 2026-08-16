#!/usr/bin/env ruby
# Generates the 64x64 RPG demo world. Deterministic (fixed seed).
#
# Run:
#   ruby flash/app/game/rpg_demo/generate_world.rb
#
# Tile IDs match world.bmp (generate_tiles.rb):
#   0 plain   1 forest  2 desert  3 sea
#   4 hill    5 mountain 6 cave    7 castle
#
# Output (world.map.json) includes a "walkable_tiles" array. The app uses it
# to block movement onto sea/mountain. Cave/castle tiles are walkable so the
# player can step onto the entrance and trigger an event.

require "json"
require "rbconfig"

WIDTH      = 64
HEIGHT     = 64
TILE_SIZE  = 16
TILESHEET  = "/app/game/rpg_demo/world.bmp"
SHEET_COLS = 4
SEED       = 0xD24A

PLAIN, FOREST, DESERT, SEA, HILL, MOUNTAIN, CAVE, CASTLE = (0..7).to_a
WALKABLE = [PLAIN, FOREST, DESERT, HILL, CAVE, CASTLE].freeze

srand(SEED)
ground = Array.new(HEIGHT) { Array.new(WIDTH, PLAIN) }

# -----------------------------------------------------------------------------
# Step 1: sea border. Encircle the world with ocean so the player can't fall
# off the edge. Coastline is jittered with random sin-wave displacement.
# -----------------------------------------------------------------------------
SEA_BAND = 6
HEIGHT.times do |y|
  WIDTH.times do |x|
    edge = [x, y, WIDTH - 1 - x, HEIGHT - 1 - y].min
    wobble = (Math.sin(x * 0.3 + y * 0.2) * 1.6).to_i
    ground[y][x] = SEA if edge + wobble < SEA_BAND
  end
end

# -----------------------------------------------------------------------------
# Step 2: mountain ranges. Two ridges of irregular blob shape across the
# upper third and one wrapping the east coast.
# -----------------------------------------------------------------------------
def blob!(ground, cx, cy, radius, tile)
  rmax = radius + 1
  ((-rmax)..rmax).each do |dy|
    ((-rmax)..rmax).each do |dx|
      next if dx * dx + dy * dy > radius * radius
      nx = cx + dx
      ny = cy + dy
      next if nx < 0 || ny < 0 || nx >= WIDTH || ny >= HEIGHT
      next if ground[ny][nx] == SEA  # preserve coastline
      ground[ny][nx] = tile
    end
  end
end

# Northern mountain ridge: chain of blobs from west to east around y=12.
0.step(WIDTH - 1, 5) do |x|
  cy = 12 + (Math.sin(x * 0.4) * 2).to_i
  blob!(ground, x, cy, 2 + rand(2), MOUNTAIN)
end
# Eastern range guarding the right side around x=50.
0.step(HEIGHT - 1, 5) do |y|
  cx = 50 + (Math.cos(y * 0.4) * 2).to_i
  blob!(ground, cx, y, 2 + rand(2), MOUNTAIN)
end

# -----------------------------------------------------------------------------
# Step 3: desert. Cluster in the south-west quadrant (warm climate).
# -----------------------------------------------------------------------------
12.times do
  cx = 6 + rand(20)
  cy = 42 + rand(16)
  blob!(ground, cx, cy, 3 + rand(3), DESERT)
end

# -----------------------------------------------------------------------------
# Step 4: forests. Scatter clusters mostly in the middle/north band.
# -----------------------------------------------------------------------------
22.times do
  cx = rand(WIDTH)
  cy = rand(HEIGHT)
  next if ground[cy][cx] != PLAIN
  blob!(ground, cx, cy, 2 + rand(3), FOREST)
end

# -----------------------------------------------------------------------------
# Step 5: hills. Scatter on remaining plain.
# -----------------------------------------------------------------------------
30.times do
  cx = rand(WIDTH)
  cy = rand(HEIGHT)
  next if ground[cy][cx] != PLAIN
  ground[cy][cx] = HILL
end

# -----------------------------------------------------------------------------
# Step 6: caves. Two caves, embedded in mountain regions so they look like
# entrances to the rock face. Replace one mountain tile with CAVE.
# -----------------------------------------------------------------------------
def place_at_or_near_tile(ground, ox, oy, target, max_radius = 6)
  return [ox, oy] if ground[oy][ox] == target
  (1..max_radius).each do |r|
    (-r..r).each do |dy|
      (-r..r).each do |dx|
        next if dx.abs != r && dy.abs != r
        nx = ox + dx
        ny = oy + dy
        next if nx < 0 || ny < 0 || nx >= WIDTH || ny >= HEIGHT
        return [nx, ny] if ground[ny][nx] == target
      end
    end
  end
  nil
end

CAVE_SPOTS = [[18, 12], [50, 30]]
caves = []
CAVE_SPOTS.each do |sx, sy|
  pos = place_at_or_near_tile(ground, sx, sy, MOUNTAIN)
  next if pos.nil?
  ground[pos[1]][pos[0]] = CAVE
  caves << pos
end

# -----------------------------------------------------------------------------
# Step 7: castle. Centered on the map (Tantegel-style starting town).
# Player spawns at the south side of the castle (its "town gate").
# -----------------------------------------------------------------------------
CASTLE_X = WIDTH / 2
CASTLE_Y = HEIGHT / 2
# Clear a small plain plaza first.
(-2..2).each do |dy|
  (-2..2).each do |dx|
    ny = CASTLE_Y + dy
    nx = CASTLE_X + dx
    ground[ny][nx] = PLAIN if ground[ny][nx] != SEA
  end
end
ground[CASTLE_Y][CASTLE_X] = CASTLE

# -----------------------------------------------------------------------------
# Events. Keep it short - only narrative POIs.
# -----------------------------------------------------------------------------
events = []
events << {"x" => CASTLE_X, "y" => CASTLE_Y, "id" => 1, "data" => {"name" => "tantegel_castle"}}
caves.each_with_index do |(cx, cy), i|
  events << {"x" => cx, "y" => cy, "id" => 10 + i, "data" => {"name" => "cave_#{i + 1}"}}
end

# Player spawn: just south of the castle, walkable plain.
spawn_x = CASTLE_X
spawn_y = CASTLE_Y + 1
spawn_y = CASTLE_Y - 1 if !WALKABLE.include?(ground[spawn_y][spawn_x])

out = {
  "format"          => "fmrb_map",
  "version"         => 1,
  "tilesheet"       => TILESHEET,
  "tilesheet_cols"  => SHEET_COLS,
  "tile_size"       => TILE_SIZE,
  "width"           => WIDTH,
  "height"          => HEIGHT,
  "walkable_tiles"  => WALKABLE,
  "spawn"           => {"x" => spawn_x, "y" => spawn_y},
  "layers"          => [{"name" => "ground", "data" => ground}],
  "events"          => events,
}

out_path = File.join(__dir__, "world.map.json")
File.write(out_path, JSON.generate(out))

# The app loads the packed copy, so regenerate it in the same breath -- a JSON
# left ahead of its .bin would look like a generator that changed nothing.
bin_tool = File.expand_path("../../../../tool/map_json2bin.rb", __dir__)
system(RbConfig.ruby, bin_tool, out_path) or abort("map_json2bin failed")

counts = Hash.new(0)
ground.each { |row| row.each { |id| counts[id] += 1 } }
puts "Wrote #{out_path} (#{WIDTH}x#{HEIGHT})"
puts "  tiles: " + counts.sort.map { |id, n| "#{id}=#{n}" }.join(", ")
puts "  caves: #{caves.size}, castle at (#{CASTLE_X},#{CASTLE_Y}), spawn at (#{spawn_x},#{spawn_y})"
puts "  events: #{events.size}"
