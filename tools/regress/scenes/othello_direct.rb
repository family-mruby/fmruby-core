# frozen_string_literal: true
#
# Othello (direct) (flash/app/test/othello.app.rb), one of the five apps
# app_refactor R6 moves onto FmrbText / the click targets: the lobby with
# two peers (one with a long name), an invitation coming in, a game
# (the opponent's move arriving, a click, the resign question), the end,
# the name being edited. The small boards (under 320 wide) only watch the
# states others put.

module OthelloDirectScenes
  ME = "fmruby-a1b2c3"
  P1 = "fmruby-d4e5f6"
  P2 = "wasm-0f0f0f"
  LONG = "とても長い名前のプレイヤーさん"

  module_function

  def peers(s)
    s.host.tables["#{P1}/game/othello"] = nil
    s.host.tables["#{P2}/game/othello"] = nil
    s.host.answer(:info) do |_|
      { "name" => LONG, "status" => "idle", "opponent" => nil }
    end
  end

  def lobby(s)
    peers(s)
    s.start
    s.run(1500)
  end

  # Invited by P1, accepted: the game is on (P1 asked; this side draws the
  # colours from the clock: black at this fixed time).
  def in_game(s)
    s.host.answer(:answer, true)
    lobby(s)
    s.net.call_exposed("othello", :invite, "Alice", "#{P1}-1", from: P1)
    s.update
    s.key "y"
    s.update(2)
  end
end

Regress.app "othello_direct", "flash/app/test/othello.app.rb" do
  scene "lobby", on: ->(t) { t.screen[0] >= 320 } do |s|
    OthelloDirectScenes.lobby(s)
    s.key :down
    s.update
  end

  scene "lobby_ja", lang: "ja", on: ->(t) { t.screen[0] >= 320 } do |s|
    OthelloDirectScenes.lobby(s)
  end

  scene "invited", on: ->(t) { t.screen[0] >= 320 } do |s|
    OthelloDirectScenes.lobby(s)
    s.net.call_exposed("othello", :invite, OthelloDirectScenes::LONG, "#{OthelloDirectScenes::P1}-1",
                       from: OthelloDirectScenes::P1)
    s.update
  end

  scene "inviting", on: ->(t) { t.screen[0] >= 320 } do |s|
    s.host.answer(:invite, "asked")
    OthelloDirectScenes.lobby(s)
    s.key :enter
    s.update(2)
  end

  scene "game", on: ->(t) { t.screen[0] >= 320 } do |s|
    OthelloDirectScenes.in_game(s)
    g = s.ivar(:@g)
    opp = g && g.turn != s.ivar(:@my)
    if opp
      # The opponent's move (the first legal square).
      i = g.board.moves(g.turn).first
      s.net.call_exposed("othello", :move, s.ivar(:@gid), g.n, i % 8, i / 8, from: OthelloDirectScenes::P1)
      s.update
    end
    s.key :right
    s.update
    bx = s.ivar(:@bx)
    by = s.ivar(:@by)
    c = s.ivar(:@cell)
    s.click(bx + c * 2 + 2, by + c * 3 + 2)
    s.update(2)
  end

  scene "resign_confirm", on: ->(t) { t.screen[0] >= 320 } do |s|
    OthelloDirectScenes.in_game(s)
    s.key :esc
    s.update
  end

  scene "over", on: ->(t) { t.screen[0] >= 320 } do |s|
    OthelloDirectScenes.in_game(s)
    s.net.call_exposed("othello", :resign, s.ivar(:@gid), from: OthelloDirectScenes::P1)
    s.update
  end

  scene "name_edit", on: ->(t) { t.screen[0] >= 320 } do |s|
    OthelloDirectScenes.lobby(s)
    s.key :f2
    s.type("Kishima the very long name")
    s.update
  end

  scene "small_watching", on: ->(t) { t.screen[0] < 320 } do |s|
    s.start
    s.update(2)
    rows = %w[-------- -------- --BBB--- ---BW--- ---WB--- -------- -------- --------]
    s.net.put("asterism/#{OthelloDirectScenes::P1}/game/othello/state",
              { "board" => rows, "black_count" => 5, "white_count" => 3, "turn" => "white", "over" => false })
    s.update
  end
end
