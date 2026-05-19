#!/usr/bin/env ruby
# Generates a 64x64 world.map.json for the RPG demo.
#
# Run with:
#   ruby flash/app/game/rpg_demo/generate_world.rb
#
# Tile IDs are inferred from the original 11x11 world.map.json. The unused
# sheet IDs (3, 4, 6, 7) are avoided because their visual meaning is unknown.
# Adjust GRASS / DECOR / PATTERN_A / PATTERN_B if the resulting map looks wrong.

require "json"

WIDTH      = 64
HEIGHT     = 64
TILE_SIZE  = 16
TILESHEET  = "/app/game/rpg_demo/world.bmp"
SHEET_COLS = 4
SEED       = 12345

# Tile IDs (from inspecting original world.map.json).
GRASS     = 2   # base terrain
DECOR     = 5   # decoration clusters (forest/rocks)
PATTERN_A = 0   # path tile A (checkerboarded with B)
PATTERN_B = 1   # path tile B

# The original 11x11 island, embedded at the center as a spawn area.
CENTER_ISLAND = [
  [2,2,2,2,2,2,2,2,2,2,2],
  [2,5,5,2,2,2,2,2,5,5,2],
  [2,5,5,2,0,1,0,2,5,5,2],
  [2,2,2,2,1,0,1,2,2,2,2],
  [2,2,2,2,0,1,0,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2],
  [2,5,5,5,5,5,5,5,5,5,2],
  [2,5,5,5,5,5,5,5,5,5,2],
  [2,2,2,2,2,2,2,2,2,2,2],
].freeze

ORIGINAL_EVENTS = [
  {"x" => 5,  "y" => 3,  "id" => 1, "data" => {"name" => "npc_chick"}},
  {"x" => 5,  "y" => 9,  "id" => 2, "data" => {"name" => "lake"}},
  {"x" => 0,  "y" => 0,  "id" => 3, "data" => {"name" => "start"}},
  {"x" => 10, "y" => 10, "id" => 4, "data" => {"name" => "goal"}},
].freeze

ORIG_W = CENTER_ISLAND[0].size
ORIG_H = CENTER_ISLAND.size
OX     = (WIDTH  - ORIG_W) / 2
OY     = (HEIGHT - ORIG_H) / 2

srand(SEED)

ground = Array.new(HEIGHT) { Array.new(WIDTH, GRASS) }

# Random walk cluster placement.
def place_cluster(ground, cx, cy, size, tile)
  size.times do
    ground[cy][cx] = tile if cx.between?(0, WIDTH - 1) && cy.between?(0, HEIGHT - 1)
    cx += rand(3) - 1
    cy += rand(3) - 1
  end
end

# Scatter decor clusters across the map (skipped if they'd land on the
# center island; the island gets re-stamped at the end anyway).
30.times do
  cx   = rand(WIDTH)
  cy   = rand(HEIGHT)
  size = 3 + rand(4)
  place_cluster(ground, cx, cy, size, DECOR)
end

# Cross-shaped road. PATTERN_A/B alternate so the path is visually obvious.
center_x = WIDTH  / 2
center_y = HEIGHT / 2

WIDTH.times do |x|
  next if x >= OX && x < OX + ORIG_W
  ground[center_y][x] = x.even? ? PATTERN_A : PATTERN_B
end
HEIGHT.times do |y|
  next if y >= OY && y < OY + ORIG_H
  ground[y][center_x] = y.even? ? PATTERN_A : PATTERN_B
end

# Re-stamp the original center island so it overrides scattered clusters
# and the road that would have crossed it.
ORIG_H.times do |y|
  ORIG_W.times do |x|
    ground[OY + y][OX + x] = CENTER_ISLAND[y][x]
  end
end

events = ORIGINAL_EVENTS.map do |ev|
  {"x" => ev["x"] + OX, "y" => ev["y"] + OY, "id" => ev["id"], "data" => ev["data"]}
end

out = {
  "format"         => "fmrb_map",
  "version"        => 1,
  "tilesheet"      => TILESHEET,
  "tilesheet_cols" => SHEET_COLS,
  "tile_size"      => TILE_SIZE,
  "width"          => WIDTH,
  "height"         => HEIGHT,
  "layers"         => [{"name" => "ground", "data" => ground}],
  "events"         => events,
}

out_path = File.join(__dir__, "world.map.json")
File.write(out_path, JSON.generate(out))
puts "Wrote #{out_path} (#{WIDTH}x#{HEIGHT}, events=#{events.size}, center_island_at=(#{OX},#{OY}))"
