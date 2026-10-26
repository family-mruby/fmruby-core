# frozen_string_literal: true
#
# Helpers for the live scenes (rake regress:live, doc/regression/usage.md
# chapter 10): how the table apps are driven and how the tables are
# summarised. A summary keeps what the scene caused and leaves out what
# moves on its own: names (the device's comes from the profile and from
# /home), tokens, versions, times, the wall of a mahjong game.

Regress::Live.helpers do
  # --- the table apps ---------------------------------------------------------

  # Open a table app and wait until it lists the counterpart's table.
  def open_table_app(path, tag, games)
    launch path
    wait_log(%r{#{tag}: table #{Regexp.escape(games.path)} on the list}, timeout: 20)
    pause 300
  end

  def members(games) = games.table["mb"] || []

  # Enter on the list: the app joins (mahjong asks the player's name before
  # its first join when /home has none: a second Enter takes the one offered).
  def join_table(games, tag)
    key "enter"
    begin
      wait_log(/#{tag}: joined /, timeout: 5)
    rescue Regress::Screens::Failure
      key "enter"
      wait_log(/#{tag}: joined /, timeout: 10)
    end
    poll("the device among the table's members") { members(games).size == 1 }
  end

  # Take the host away and bring it back: the app sees it leave and come
  # back (Asterism's on_leave / on_join) and goes on at its table.
  def restart_host(games, tag)
    games.stop
    wait_log(/#{tag}: #{Regexp.escape(games.path.split('/').first)} left the network/, timeout: 20)
    games.resume
    wait_log(/#{tag}: #{Regexp.escape(games.path.split('/').first)} is back/, timeout: 30)
  end

  # --- othello ----------------------------------------------------------------

  # The device's seat: the member's seat in the table's view ([name, seat, flags]).
  def othello_seat(games)
    m = members(games).first
    m ? m[1] : -1
  end

  # The centre of a square ("d3") on the Modern window's board: 8 x 25 px
  # from (32, 30).
  def othello_square(sq)
    col = sq.getbyte(0) - "a".ord
    row = sq[1].to_i - 1
    [32 + 25 * col + 12, 30 + 25 * row + 12]
  end

  # On the device's turn, click the first legal square; done when the host
  # has the move and the CPU has answered (the device to move again, or the
  # game over). A click the app sends with an old view comes back "stale"
  # and is clicked again.
  def othello_move(games)
    me = othello_seat(games)
    t = poll("the device's turn", timeout: 15) do
      v = games.table
      v["st"] == "play" && v["tn"] == me && v
    end
    sq = t["lg"][0, 2]
    3.times do
      click(*othello_square(sq))
      done = begin
        poll("the move #{sq} and the CPU's answer", timeout: 4) do
          v = games.table
          v["n"].to_i >= t["n"].to_i + 2 && (v["tn"] == me || v["st"] != "play")
        end
      rescue Regress::Screens::Failure
        nil
      end
      return sq if done
    end
    raise Regress::Screens::Failure, "the host never took the device's move #{sq}"
  end

  # The table seen from the device: its seat and colour, the board in D
  # (the device's discs) and O (the other side's), whose turn.
  def othello_summary(games)
    v = games.table
    me = othello_seat(games)
    who = ->(seat) { seat == me ? "device" : { 0 => "empty", 1 => "member", 2 => "cpu" }[v["sk"][seat]] }
    out = { "st" => v["st"], "members" => (v["mb"] || []).size }
    if me.negative? # not at the table: what is left there (the seats in no order: the colours were drawn)
      out["seats"] = [0, 1].map { |i| who.call(i) }.sort
      out["result"] = v["res"] && v["res"]["why"]
      return out
    end
    out["seats"] = [0, 1].map { |i| who.call(i) }
    out["owner"] = v["ow"] == me
    if v["bd"]
      mine = me.zero? ? "B" : "W"
      out["board"] = v["bd"].tr(mine == "B" ? "BW" : "WB", "DO").scan(/.{8}/)
      out["moves"] = v["n"]
      out["turn"] = v["tn"] == me ? "device" : (v["tn"].to_i.negative? ? "none" : "other")
      out["result"] = v["res"] && { "winner" => v["res"]["w"] == me ? "device" : "other", "why" => v["res"]["why"] }
    end
    out
  end

  # The device's colour, for the variant of a summary (the host draws it).
  def othello_colour(games) = othello_seat(games).zero? ? "black" : "white"

  # --- the drawing board --------------------------------------------------------

  # A stroke on the board's drawing area (the Modern window), as a drag.
  def board_stroke(points)
    press(*points.first)
    points[1..].each { |x, y| move(x, y) }
    release(*points.last)
  end

  def board_summary(games)
    v = games.table
    out = { "st" => v["st"], "strokes" => v["n"], "members" => (v["mb"] || []).map { |_n, f| f & 2 == 2 ? "owner" : "member" } }
    pic = v["n"].to_i.positive? ? games.call(:picture, 0, 0) : { "p" => [] }
    # Each stroke: its colour, width and first and last points (how many
    # points come between depends on how fast the motion was sampled).
    out["picture"] = pic["p"].map { |_id, c, w, pts, _k| { "c" => c, "w" => w, "from" => pts[0, 2].join(","), "to" => pts[-2, 2].join(",") } }
    out
  end

  # --- mahjong ----------------------------------------------------------------

  def mahjong_look(games) = games.call(:look, games.token)

  # The device's discard: wait for its turn (passing a call offered on the
  # way, going on after a round's result), Enter (the drawn tile), and the
  # host has one more tile in the device's river.
  def mahjong_discard(games)
    look = poll("the device's turn to discard", timeout: 40, every: 0.4) do
      l = mahjong_look(games)
      types = (l["moves"] || []).map { |m| m["type"] }
      if types.include?("discard")
        l
      else
        key "esc" if types.include?("pass")     # a call offered: pass it
        key "enter" if types.include?("next")   # a round's result: go on
        nil
      end
    end
    me = look["you"]
    before = look["seats"][me]["river"].size
    key "enter"
    poll("the device's discard at the host", timeout: 6) do
      mahjong_look(games)["seats"][me]["river"].size > before
    end
  end

  def mahjong_summary(games)
    v = games.table
    out = { "st" => v["st"], "members" => (v["mb"] || []).size,
            "seats" => (v["sk"] || []).map { |k| { 0 => "empty", 1 => "member", 2 => "cpu" }[k] }.sort }
    if v["st"] == "play" && games.token
      l = mahjong_look(games)
      me = l["seats"][l["you"]]
      out["device"] = { "kind" => me["kind"], "discarded" => !me["river"].empty? }
    end
    out
  end
end
