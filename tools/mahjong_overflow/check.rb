#!/usr/bin/env ruby
# frozen_string_literal: true
#
# check.rb - the mahjong table's screen against its worst cases
# (doc/ruby_asterism/report/mj5.md, "overflow checker").
#
#   ruby tools/mahjong_overflow/check.rb            # 1280x720, 640x360, 426x240
#   ruby tools/mahjong_overflow/check.rb --size 640x360 --case result_triple_ron -v
#   rake mahjong:overflow                           # the same (also in rake test)
#
# It loads the table app's own files (flash/app/game/mahjong/*.rb) into
# CRuby with a stand-in for the graphics that records every rectangle,
# tile, triangle and text (its extent from the font size: a half-width
# character half the full width, as the app's tw), and draws each case of
# fixtures.rb at each size from a clean screen. A case fails when anything
# drawn
#   1. goes off the screen,
#   2. goes outside its panel (the part of the screen the drawing method
#      belongs to: a river, a row, the board, the buttons, a result box ...),
#   3. overlaps something of another panel (a result box and what is in it
#      are drawn over the table and exempt), or
#   4. is a text that overlaps another text or a tile.
# 1280x720 and 640x360 fail the run; 426x240 (the sim's small table) only
# warns, but must draw without an exception. Exit status 1 on a failure.

require_relative "fixtures"

CORE = File.expand_path("../..", __dir__)
APP_DIR = File.join(CORE, "flash", "app", "game", "mahjong")

# ---------------------------------------------------------------- stand-ins

module FmrbConst
  HW_FAMILY = "modern"
  PLATFORM = "wasm"
  KEY_UP = 0x52
  KEY_DOWN = 0x51
  KEY_LEFT = 0x50
  KEY_RIGHT = 0x4F
  KEY_ENTER = 0x28
  KEY_ESC = 0x29
  KEY_TAB = 0x2B
end

module Machine
  def self.board_millis
    1_000_000
  end
end

module Log
  def self.info(*) = nil
  def self.error(*) = nil
end

class SpriteImage
  @@n = 0
  attr_reader :id

  def initialize(_gfx, **) = (@id = (@@n += 1))
  def load_bmp(_path) = nil
end

class FmrbApp
  def self.wifi_info = nil
  def self.pool_used = 0
  def self.pool_usage = 0

  def clear_user_area(_c)
    @gfx.record(:clear, @user_area_x0, @user_area_y0, @user_area_width, @user_area_height)
  end
end

# The graphics: records what is drawn, with the scope (panel) it was drawn in.
class RecGfx
  Prim = Struct.new(:kind, :x, :y, :w, :h, :panel, :rect, :overlay, :text)

  attr_reader :prims

  def initialize(scopes)
    @scopes = scopes
    @prims = []
    @px = 12
    @size = 1
  end

  def set_font(_f, px) = (@px = px)
  def set_text_size(n) = (@size = n)
  def present = nil
  def sync_file(*, **) = nil

  def text_w(s)
    w = 0
    s.each_char { |ch| w += ch.bytesize == 1 ? @px * @size / 2 : @px * @size }
    w
  end

  def draw_text(x, y, s, _c)
    return if s.to_s.empty?
    record(:text, x, y, text_w(s.to_s), @px * @size, s.to_s)
  end

  def fill_rect(x, y, w, h, _c) = record(:fill, x, y, w, h)
  def draw_rect(x, y, w, h, _c) = record(:frame, x, y, w, h)
  def fill_round_rect(x, y, w, h, _r, _c) = record(:fill, x, y, w, h)
  def draw_round_rect(x, y, w, h, _r, _c) = record(:frame, x, y, w, h)

  def fill_triangle(x1, y1, x2, y2, x3, y3, _c)
    xs = [x1, x2, x3]
    ys = [y1, y2, y3]
    record(:tri, xs.min, ys.min, xs.max - xs.min + 1, ys.max - ys.min + 1)
  end

  def draw_tile(_id, _sx, _sy, w, h, dst_x:, dst_y:) = record(:tile, dst_x, dst_y, w, h)

  def record(kind, x, y, w, h, text = nil)
    top = @scopes.last
    if top && top[:box] == :first && kind == :fill
      top[:box] = [x, y, w, h]
    end
    rect = top && (top[:box].is_a?(Array) ? top[:box] : top[:rect])
    @prims << Prim.new(kind, x, y, w, h, top && top[:name], rect, top && top[:overlay], text)
  end
end

# ---------------------------------------------------------------- the app

def load_app
  %w[mahjong_xl.rb mahjong_panels.rb mahjong_watch.rb].each { |f| eval(File.read(File.join(APP_DIR, f)), TOPLEVEL_BINDING, f) }
  src = File.read(File.join(APP_DIR, "mahjong.app.rb"))
  src = src.lines.reject { |l| l.start_with?("require ") }.join
  src = src.sub(/^begin\n  app = MahjongApp.new.*\z/m, "")
  eval(src, TOPLEVEL_BINDING, "mahjong.app.rb")
end

load_app

# The panels: which method draws which part of the screen, and where that
# part is. A panel rect of nil is the whole screen; :first is a box whose
# rect is its own first fill (the result boxes, drawn over the table).
module Panels
  BIG = {
    xl_draw_board: ->(a, _) { ["board", [a.class::XL_BX, a.class::XL_BY, a.class::XL_BW, a.class::XL_BH]] },
    xl_draw_status: ->(a, _) { ["status", a.class::XL_STATUS] },
    xl_draw_waits: ->(a, _) { ["waits", a.class::XL_WAITS] },
    xl_draw_buttons: ->(a, _) { ["buttons", a.xl_panel(:buttons)] },
    xl_draw_result: ->(_, _) { ["result", :first, true] },
    draw_result: ->(_, _) { ["result", :first, true] },
    draw_info: ->(a, _) { ["info", a.panel640(:info)] },
    draw_hand: ->(a, _) { ["hand", a.panel640(:hand)] },
    draw_controls: ->(a, _) { ["controls", a.panel640(:controls)] },
    draw_meld_tiles: ->(a, args) { ["melds#{args[0]}", a.panel640(:"melds#{args[0]}")] },
    draw_assist: ->(a, _) { ["assist", a.panel640(:assist)] },
    xl_draw_assist: ->(a, _) { ["assist", a.xl_panel(:assist)] },
    draw_spec_hand: lambda { |a, args|
      rl = args[0]
      r = a.panel640(:"w_row#{rl}")
      b = a.panel640(:"w_row#{rl}b")
      if b
        x0 = [r[0], b[0]].min
        y0 = [r[1], b[1]].min
        r = [x0, y0, [r[0] + r[2], b[0] + b[2]].max - x0, [r[1] + r[3], b[1] + b[3]].max - y0]
      end
      ["row#{rl}", r]
    },
    draw_watch_river: ->(a, args) { ["river#{args[0]}", a.panel640(:"w_river#{args[0]}")] },
    draw_watch_board: ->(a, _) { ["board", a.panel640(:w_board)] },
    draw_score_strip: ->(a, _) { ["strip", a.panel640(:w_strip)] },
    draw_watch: ->(_, _) { ["watch", nil] },
    draw_list: ->(_, _) { ["list", nil] },
    draw_lobby: ->(_, _) { ["lobby", nil] },
    draw_rules: ->(a, _) { ["rules", a.panel640(:rules)&.map { |v| v * a.instance_variable_get(:@k) }] },
    draw_status: ->(_, _) { ["status", nil] }
  }.freeze
end

class MahjongApp
  attr_accessor :scopes

  # 640x360 (and the small table's) parts, from the drawing code's own
  # numbers; at 1280x720 the parts come from the app (xl_panel).
  def panel640(key)
    return nil unless @big
    respond_to?(:layout640) ? layout640(key) : nil
  end
end

module Scoping
  Panels::BIG.each do |m, f|
    define_method(m) do |*args, **kw, &blk|
      name, rect, overlay = f.call(self, args)
      @scopes << { name: name, rect: rect, box: rect == :first ? :first : nil, overlay: overlay }
      begin
        kw.empty? ? super(*args, &blk) : super(*args, **kw, &blk)
      ensure
        @scopes.pop
      end
    end
  end

  # The 640x360 rivers and seat labels: one method draws all four; the
  # area each starts from names its panel.
  def river_area(rl)
    a = super
    @scopes.pop if @scopes.last && @scopes.last[:sub]
    @scopes << { name: "river#{rl}", rect: panel640(:"river#{rl}"), sub: true } if @big && !@xl
    a
  end

  def seat_label_pos(rl)
    p = super
    @scopes.pop if @scopes.last && @scopes.last[:sub]
    @scopes << { name: "label#{rl}", rect: panel640(:"label#{rl}"), sub: true } if @big && !@xl
    p
  end

  %i[draw_rivers draw_seats].each do |m|
    define_method(m) do |*args|
      n = @scopes.size
      super(*args)
    ensure
      @scopes.pop while @scopes.size > n
    end
  end

  # The 1280x720 items: each part is a panel.
  def xl_draw_item(it)
    part = @xl_part_of && @xl_part_of[it.object_id]
    if part
      @scopes << { name: "part:#{part}", rect: xl_panel(part.to_sym) }
      begin
        super
      ensure
        @scopes.pop
      end
    else
      super
    end
  end

  def xl_parts
    p = super
    @xl_part_of = {}
    p.each { |k, items| items.each { |it| @xl_part_of[it.object_id] = k } }
    p
  end
end
MahjongApp.prepend(Scoping)

# ---------------------------------------------------------------- checking

def new_app(w, h)
  app = MahjongApp.allocate
  app.scopes = []
  gfx = RecGfx.new(app.scopes)
  app.instance_variable_set(:@gfx, gfx)
  app.instance_variable_set(:@user_area_x0, 0)
  app.instance_variable_set(:@user_area_y0, 0)
  app.instance_variable_set(:@user_area_width, w)
  app.instance_variable_set(:@user_area_height, h)
  app.setup_strings
  {
    started: 0, dirty: true, keys: [], clicks: [], buttons: [], unsupported: false, hand_sheet: :sheet,
    river_sheet: nil, xl_sheets: false, xl_items: nil, fw: 12, perf_on: false, perf: [], xl_n_items: 0, xl_n_px: 0,
    id: "wasm-aaaaaa", name: MjFixtures::LONG_JA[0, 12], name_set: MjFixtures::LONG_JA[0, 12], name_machine: "", name_edit: nil,
    pending_join: nil, net: :up, net_note: "", tables: {}, table_list: [], list_sel: 0,
    probed: [], mine: {}, sub: nil, table: "games-host-aaaaaa/games/mahjong1", token: "t", seat: nil, view: nil,
    view_at: 0, want_v: 0, host_away: false, note: "", sel: -1, mouse_sel: false, waits: {}, uk: {}, uk_min: 9,
    assist: 1, pick: false, lat: 61, deadline: nil, bank_ms: 0, others: false, second: 0, mem_at: 0
  }.each { |k, v| app.instance_variable_set(:"@#{k}", v) }
  app.layout
  # Sheets: real ids so the tiles go through draw_tile.
  app.instance_variable_set(:@hand_sheet, SpriteImage.new(nil))
  app.instance_variable_set(:@river_sheet, SpriteImage.new(nil))
  if app.instance_variable_get(:@xl)
    app.instance_variable_set(:@xl_sheets, true)
    app.instance_variable_set(:@xl_hand, [SpriteImage.new(nil), SpriteImage.new(nil)])
    app.instance_variable_set(:@xl_r2, Array.new(4) { SpriteImage.new(nil) })
    app.instance_variable_set(:@xl_r1, Array.new(4) { SpriteImage.new(nil) })
    app.instance_variable_set(:@xl_side, SpriteImage.new(nil))
  end
  [app, gfx]
end

def inside?(p, r)
  return true if r.nil?
  p.x >= r[0] && p.y >= r[1] && p.x + p.w <= r[0] + r[2] && p.y + p.h <= r[1] + r[3]
end

def overlap?(a, b)
  a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h
end

def desc(p)
  s = "#{p.kind} [#{p.x},#{p.y} #{p.w}x#{p.h}] in #{p.panel || '-'}"
  s += " #{p.text.inspect}" if p.text
  s
end

# The problems of one drawing: [String].
def problems(prims, w, h)
  out = []
  list = prims.reject { |p| p.kind == :clear }
  list.each do |p|
    out << "off the screen: #{desc(p)}" if p.x.negative? || p.y.negative? || p.x + p.w > w || p.y + p.h > h
    out << "outside its panel #{p.rect.inspect}: #{desc(p)}" unless inside?(p, p.rect)
  end
  # A box drawn over the table covers what is under it: only the drawing
  # after it (inside it) counts from there on.
  boxes = list.select { |p| p.overlay }
  table = list.reject(&:overlay)
  table = table.reject { |p| boxes.any? { |b| b.rect && overlap?(p, Struct.new(:x, :y, :w, :h).new(*b.rect)) } } unless boxes.empty?
  [table, boxes].each do |group|
    group.each_with_index do |a, i|
      group[(i + 1)..].each do |b|
        next unless overlap?(a, b)
        if a.panel != b.panel && !(a.panel.nil? || b.panel.nil?)
          out << "panels overlap: #{desc(a)} / #{desc(b)}"
        elsif (a.kind == :text && %i[text tile].include?(b.kind)) || (b.kind == :text && %i[text tile].include?(a.kind))
          out << "text overlaps: #{desc(a)} / #{desc(b)}"
        end
      end
    end
  end
  out.uniq
end

# Draws one case at one size; returns [problems, prims].
def run_case(view, w, h, prep = nil)
  app, gfx = new_app(w, h)
  me = view["me"]
  app.instance_variable_set(:@seat, me.is_a?(Integer) && me >= 0 ? me : nil)
  app.take_view(view)
  prep&.call(app)
  app.instance_variable_set(:@buttons, [])
  app.draw_screen
  [problems(gfx.prims, w, h), gfx.prims]
end

SIZES = { "1280x720" => :fail, "640x360" => :fail, "426x240" => :warn }.freeze

# Extra drawings of some cases: [suffix, preparation].
EXTRAS = {
  "lobby_owner" => [["page2", ->(a) { a.instance_variable_set(:@rule_page, 1) if a.respond_to?(:rule_pages) }],
                    ["name_edit", ->(a) { a.instance_variable_set(:@name_edit, MjFixtures::LONG_JA[0, 12]) }]],
  "lobby_member" => [["name_ascii", ->(a) { a.instance_variable_set(:@name, "WWWWWWWWWWWW") }],
                     ["list_prompt", lambda { |a|
                       tables = %w[games-host-aaaaaa/games/mahjong1 games-host-aaaaaa/games/mahjong2 games/games/mahjong9]
                       a.instance_variable_set(:@table, nil)
                       a.instance_variable_set(:@table_list, tables)
                       a.instance_variable_set(:@tables, tables.to_h { |t| [t, { "st" => "play", "sk" => [1, 1, 2, 2], "mb" => 16 }] })
                       a.instance_variable_set(:@mine, { tables[0] => true })
                       a.instance_variable_set(:@name_set, nil)
                       a.instance_variable_set(:@pending_join, tables[0])
                       a.instance_variable_set(:@name_edit, MjFixtures::LONG_JA[0, 12])
                     }],
                     ["list_unset", lambda { |a|
                       a.instance_variable_set(:@table, nil)
                       a.instance_variable_set(:@table_list, %w[games-host-aaaaaa/games/mahjong1])
                       a.instance_variable_set(:@name_set, nil)
                       a.instance_variable_set(:@note, "この部屋での名乗りが合わない (部屋のリンクを開き直す)")
                     }]],
  "rivers_longest_turn" => [["chosen", ->(a) { a.instance_variable_set(:@sel, 2) }]],
  "assist_ukeire" => [["chosen", ->(a) { a.instance_variable_set(:@sel, 0) }]]
}.freeze

if $PROGRAM_NAME == __FILE__
  sizes = SIZES.keys
  only = nil
  verbose = false
  dump = nil
  args = ARGV.dup
  until args.empty?
    a = args.shift
    case a
    when "--size" then sizes = [args.shift]
    when "--case" then only = args.shift
    when "-v" then verbose = true
    when "--dump" then dump = args.shift
    else abort "unknown argument #{a}"
    end
  end
  failed = 0
  warned = 0
  total = 0
  MjFixtures.all.each do |name, (view, what)|
    next if only && name != only
    runs = [[name, nil]] + (EXTRAS[name] || []).map { |suf, prep| ["#{name}/#{suf}", prep] }
    runs.each do |label, prep|
      sizes.each do |size|
        w, h = size.split("x").map(&:to_i)
        total += 1
        begin
          probs, prims = run_case(Marshal.load(Marshal.dump(view)), w, h, prep)
        rescue StandardError => e
          probs = ["exception: #{e.class}: #{e.message} (#{e.backtrace.first(3).join(' | ')})"]
          prims = []
        end
        if dump
          File.open(File.join(dump, "#{label.tr('/', '_')}_#{size}.txt"), "w") { |f| prims.each { |p| f.puts desc(p) } }
        end
        strict = SIZES[size] == :fail || probs.any? { |p| p.start_with?("exception") }
        if probs.empty?
          puts "ok    #{size}  #{label}" if verbose
          next
        end
        if strict then failed += 1 else warned += 1 end
        puts "#{strict ? 'FAIL' : 'warn'}  #{size}  #{label} (#{what}): #{probs.size}"
        probs.first(verbose ? 1000 : 6).each { |p| puts "        #{p}" }
      end
    end
  end
  puts "#{total} drawings: #{failed} failed, #{warned} warnings (426x240 is a warning only)"
  exit(failed.zero? ? 0 : 1)
end
