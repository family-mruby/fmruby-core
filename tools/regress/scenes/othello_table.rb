# frozen_string_literal: true
#
# The othello table (flash/app/game/othello/): the list, a seat in the
# lobby, a game, the resign question, the end. Modern keeps its 372x222
# window; Retro (320x240) takes the whole screen. The table apps do not
# open on the small screens (their storage leaves app/game out).

module OthelloTableScenes
  T1 = "games-host-a1b2c3/games/othello1"
  T2 = "games-host-a1b2c3/games/othello2"
  LONG = "とても長い名前のプレイヤーさん"
  START = "-" * 27 + "WB" + "-" * 6 + "BW" + "-" * 27
  MID = "--------" "--------" "--BBB---" "---BW---" "---WB---" "--------" "--------" "--------"

  module_function

  def tables(s)
    s.host.tables[T1] = { "st" => "play", "nm" => ["alice", LONG], "v" => 3 }
    s.host.tables[T2] = { "st" => "wait", "nm" => ["", ""], "v" => 1 }
  end

  # Connected, the list read and the states put.
  def listed(s)
    tables(s)
    s.start
    s.update(2)
    s.net.put_states
    s.update
  end

  # Joined othello2 with view as the host's answer.
  def joined(s, view)
    s.host.answer(:view) { |_| view }
    listed(s)
    s.key :down
    s.update
    s.key :enter
    s.update(3)
  end

  def lobby
    { "v" => 5, "st" => "wait", "me" => 0, "ow" => 0, "sk" => [1, 0], "nm" => [LONG, ""], "ak" => ["jev"] }
  end

  def play
    { "v" => 9, "st" => "play", "me" => 0, "ow" => 0, "sk" => [1, 2], "nm" => [LONG, ""], "tn" => 0, "n" => 5,
      "lg" => "c4e3f6", "bd" => MID, "cnt" => [4, 3], "last" => "c3", "dl" => 30_000 }
  end
end

Regress.app "othello_table", "flash/app/game/othello/othello.app.rb" do
  scene "list" do |s|
    OthelloTableScenes.listed(s)
    s.key :down
    s.update
  end

  scene "searching" do |s|
    s.start
    s.update(3)
  end

  scene "seated_lobby" do |s|
    OthelloTableScenes.joined(s, OthelloTableScenes.lobby)
  end

  scene "spectator" do |s|
    v = OthelloTableScenes.lobby.merge("me" => -1, "sk" => [1, 1], "nm" => ["alice", OthelloTableScenes::LONG])
    v.delete("ak")
    OthelloTableScenes.joined(s, v)
  end

  scene "playing" do |s|
    OthelloTableScenes.joined(s, OthelloTableScenes.play)
    s.key :right
    s.update
    # A click on a square that is not legal: the note says so.
    bx = s.ivar(:@bx)
    by = s.ivar(:@by)
    c = s.ivar(:@cell)
    s.click(bx + c * 7 + 2, by + c * 7 + 2)
    s.update
    s.wait(10_000)
    s.update
  end

  scene "resign_confirm" do |s|
    OthelloTableScenes.joined(s, OthelloTableScenes.play)
    s.key :esc
    s.update
  end

  scene "over" do |s|
    v = OthelloTableScenes.play.merge("st" => "over", "res" => { "w" => 1, "b" => 20, "wc" => 44, "why" => "resign" })
    v.delete("dl")
    OthelloTableScenes.joined(s, v)
  end

  scene "ai_thinking", lang: "ja" do |s|
    v = OthelloTableScenes.play.merge("tn" => 1, "ai" => ["", "jev"], "nm" => [OthelloTableScenes::LONG, "Jev"])
    OthelloTableScenes.joined(s, v)
  end
end
