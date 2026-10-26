# frozen_string_literal: true
#
# The table apps against a game host of our own (counterpart :games): the
# list, joining, a move or two, the host taken away and brought back (the
# app goes on at its seat), leaving. What R4a/R4b checked by hand
# (doc/archive/app_refactor/report/r4a.md 5.2, r4b.md 5.2).

Regress::Live.scene "othello" do |s|
  g = s.counterpart :games, table: :othello
  s.open_table_app "/app/game/othello/othello.app.rb", "othello_table", g
  s.join_table g, "othello_table"
  s.key "s"   # sit
  s.poll("the device in a seat") { s.othello_seat(g) >= 0 }
  s.key "c"   # the CPU in the other seat (the device is the owner)
  s.poll("the CPU in the other seat") { g.table["sk"].sort == [1, 2] }
  s.check "lobby", s.othello_summary(g)
  s.key "enter" # start
  s.poll("the game to start") { g.table["st"] == "play" }
  colour = s.othello_colour(g)
  s.othello_move g
  s.check "move", s.othello_summary(g), variant: colour
  s.restart_host g, "othello_table"
  s.othello_move g # back at its seat: it plays on
  s.check "after_host", s.othello_summary(g), variant: colour
  s.key "esc"  # leave: playing a seat, it asks to resign first
  s.key "y"
  s.poll("the game over") { g.table["st"] == "over" }
  s.check "resigned", s.othello_summary(g), variant: colour
  s.key "esc"
  s.poll("the device off the table") { s.members(g).empty? }
  s.check "left", s.othello_summary(g)
end

Regress::Live.scene "board" do |s|
  g = s.counterpart :games, table: :board
  s.open_table_app "/app/game/board/board.app.rb", "board", g
  s.join_table g, "board"
  s.wait_log(/board: picture read: 0 strokes/)
  s.board_stroke [[60, 80], [100, 100], [150, 120], [200, 140]]
  s.poll("the stroke at the host") { g.table["n"] == 1 }
  s.check "stroke", s.board_summary(g)
  s.restart_host g, "board"
  s.wait_log(/board: picture read: 1 strokes/, timeout: 15) # read again when the host is back
  s.board_stroke [[80, 180], [140, 170], [220, 160]]
  s.poll("the second stroke at the host") { g.table["n"] == 2 }
  s.check "after_host", s.board_summary(g)
  s.key "esc"
  s.poll("the device off the board") { s.members(g).empty? }
  s.check "left", s.board_summary(g)
end

Regress::Live.scene "mahjong" do |s|
  g = s.counterpart :games, table: :mahjong
  s.open_table_app "/app/game/mahjong/mahjong.app.rb", "mahjong", g
  s.join_table g, "mahjong"
  s.key "1"   # sit
  s.poll("the device in a seat") { g.table["sk"].include?(1) }
  s.key "c"   # CPUs in the empty seats (the owner)
  s.poll("three CPUs") { g.table["sk"].sort == [1, 2, 2, 2] }
  s.check "lobby", s.mahjong_summary(g)
  s.key "enter" # start
  s.poll("the game to start", timeout: 15) { g.table["st"] == "play" }
  s.mahjong_discard g
  s.check "discard", s.mahjong_summary(g)
  s.restart_host g, "mahjong"
  s.mahjong_discard g # back at its seat: it plays on
  s.check "after_host", s.mahjong_summary(g)
  # Ctrl+Q closes the app; the seat stays at the host (R4b: on_destroy
  # closes the connection only).
  s.key "ctrl+q"
  s.wait_log(/mahjong: closed/)
  s.check "closed", s.mahjong_summary(g).slice("st", "members", "seats")
end
