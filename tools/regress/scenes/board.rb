# frozen_string_literal: true
#
# The drawing board (flash/app/game/board/): the list, a board with a
# picture read in two pages, a stroke drawn with the mouse, another
# member's stroke and an undo arriving, the clear question.

module BoardScenes
  B1 = "games-host-a1b2c3/games/board1"
  B2 = "games-host-a1b2c3/games/board2"

  module_function

  def listed(s)
    s.host.tables[B1] = { "st" => "open", "v" => 4, "n" => 12, "mb" => 3 }
    s.host.tables[B2] = { "st" => "open", "v" => 1, "n" => 0, "mb" => 0 }
    s.start
    s.update(2)
    s.net.put_states
    s.update
  end

  # At board1 (the owner), its picture read: two strokes on page one, one on page two.
  def at_board(s, owner: true)
    s.host.answer(:join, { "token" => "tok-b1", "owner" => owner })
    s.host.answer(:view) { |_| { "v" => 4, "own" => owner, "mb" => %w[Player alice bob] } }
    s.host.answer(:picture) do |args|
      if args[0].to_i.zero?
        { "v" => 4, "p" => [[1, 4, 1, [10, 10, 200, 40, 320, 300]], [2, 11, 3, [600, 20, 40, 340]]], "nx" => [3, 0] }
      else
        { "v" => 4, "p" => [[3, 0, 2, [100, 180, 540, 180, 320, 60, 100, 180]]] }
      end
    end
    s.host.answer(:line, { "id" => 9 })
    listed(s)
    s.key :enter
    s.update(5)
  end
end

Regress.app "board", "flash/app/game/board/board.app.rb" do
  scene "list" do |s|
    BoardScenes.listed(s)
    s.key :down
    s.update
  end

  scene "board" do |s|
    BoardScenes.at_board(s)
  end

  scene "draw_stroke" do |s|
    BoardScenes.at_board(s)
    cx = s.ivar(:@cx)
    cy = s.ivar(:@cy)
    # Pick a colour and a width on the panel, then draw.
    px = s.ivar(:@px)
    s.click(px + 14 * 2 + 3, s.ivar(:@pal_y) + 14 + 3)
    s.key "3"
    s.update
    s.press(cx + 20, cy + 20)
    s.move(cx + 60, cy + 30)
    s.move(cx + 90, cy + 70)
    s.release(cx + 100, cy + 80)
    s.update(4)
  end

  scene "others" do |s|
    BoardScenes.at_board(s, owner: false)
    s.net.put("asterism/#{BoardScenes::B1}/strokes", { "o" => "a", "id" => 10, "c" => 5, "w" => 2, "p" => [0, 0, 639, 359], "v" => 5 })
    s.update
    s.net.put("asterism/#{BoardScenes::B1}/strokes", { "o" => "u", "id" => 2, "v" => 6 })
    s.update(2)
  end

  scene "clear_confirm" do |s|
    BoardScenes.at_board(s)
    s.key "c"
    s.update
  end
end
