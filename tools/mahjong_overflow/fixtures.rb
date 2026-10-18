# frozen_string_literal: true
#
# The worst cases of the mahjong table's screen (doc/ruby_asterism/report/
# mj5.md): views in the game host's wire format (asterism-games README
# "Views"), built by hand. Used by check.rb (the drawing checked against the
# screen and its panels) and fixture_host.rb (the same views served to the
# real app, for screenshots).

module MjFixtures
  NAMES = (%w[m p s].flat_map { |s| (1..9).map { |n| "#{n}#{s}" } + ["0#{s}"] } +
           (1..7).map { |n| "#{n}z" } + ["back"]).freeze

  module_function

  def c(name)
    NAMES.index(name) or raise ArgumentError, name
  end

  def bin(names)
    names.map { |n| n.is_a?(Integer) ? n : c(n) }.pack("C*").b
  end

  # A meld: kind (1 chi, 2 pon, 3 ankan, 4 daiminkan, 5 kakan), from (1
  # right, 2 across, 3 left, 0 none), the tiles in the table's order.
  def meld(kind, from, tiles)
    [kind, from, tiles.size] + tiles.map { |t| c(t) }
  end

  def melds(list)
    list.flatten.pack("C*").b
  end

  # A river of n tiles, the riichi tile at riichi (index), called away at
  # the indexes of called.
  def river(n, riichi: nil, called: [], start: 0)
    pool = %w[1z 9m 2p 8s 3z 7p 4m 6s 5z 1p 9s 2m 6z 8p 3s 7m 4z 1s 9p 2s 7z 8m 3p 5s 6m 4p 1m 9z]
    pool = pool.reject { |x| x == "9z" }
    (0...n).map do |i|
      v = c(pool[(i + start) % pool.size])
      v |= 0x40 if i == riichi
      v |= 0x80 if called.include?(i)
      v
    end.pack("C*").b
  end

  LONG_JA = "名前名前名前名前名前ab"            # 32 bytes, the host's limit
  LONG_EN = "abcdefghijklmnopqrstuvwxyz012345"  # 32 bytes

  RU = [0, 1, 3, 1, 0, 1, 1, 0, 1, 1, 5].freeze
  RX = [1, 1, 1, 1].freeze
  RY = [1, 0].freeze

  # The public part every case starts from: a table in play.
  def base(me: 0, st: "play")
    {
      "v" => 100, "sv" => 50, "t" => "mahjong1", "st" => st,
      "nm" => [LONG_JA, LONG_EN, LONG_JA, LONG_EN], "hu" => 15, "sk" => [1, 1, 1, 1], "aw" => 0b0100, "ow" => 0,
      "mb" => [[LONG_JA, 0, 2], [LONG_EN, 1, 0], [LONG_JA, 2, 1], [LONG_EN, 3, 0]] +
              (0...12).map { |i| ["#{LONG_JA[0, 8]}観戦#{i}", -1, i.odd? ? 1 : 0] },
      "ru" => RU, "rx" => RX, "ry" => RY, "tb" => 20,
      "sc" => [123_400, -12_300, 100_900, -112_000],
      "r" => [2, 4, 12, 9, 3, 7, 0, 1], "ri" => 0b1111,
      "dora" => bin(%w[1z 9m 5p 7s 3z]),
      "kw" => [river(24, riichi: 5), river(24, riichi: 11, start: 3), river(24, riichi: 17, start: 7), river(24, riichi: 23, start: 11)],
      "fu" => ["", "", "", ""], "hc" => [13, 13, 13, 13],
      "ls" => [3, c("6m")], "ev" => [1, 3], "dl" => [1, 4_000, 3_000],
      "me" => me
    }
  end

  # Four kans each (three added or open with the called tile sideways, one
  # closed), the hands one tile and the drawn one.
  def four_kans
    [
      melds([meld(5, 3, %w[1m 1m 1m 1m]), meld(4, 2, %w[2p 2p 2p 2p]), meld(5, 1, %w[3s 3s 3s 3s]), meld(3, 0, %w[7z 7z 7z 7z])]),
      melds([meld(4, 1, %w[4m 4m 4m 4m]), meld(5, 2, %w[5p 0p 5p 5p]), meld(4, 3, %w[6s 6s 6s 6s]), meld(5, 1, %w[1z 1z 1z 1z])]),
      melds([meld(5, 2, %w[7m 7m 7m 7m]), meld(5, 3, %w[8p 8p 8p 8p]), meld(4, 1, %w[9s 9s 9s 9s]), meld(4, 2, %w[2z 2z 2z 2z])]),
      melds([meld(4, 3, %w[3m 3m 3m 3m]), meld(4, 1, %w[4p 4p 4p 4p]), meld(5, 2, %w[5z 5z 5z 5z]), meld(3, 0, %w[6z 6z 6z 6z])])
    ]
  end

  def win(seat, yaku, han, pt, hand, agari, pao: nil)
    w = { "w" => seat, "y" => yaku, "yh" => han, "pt" => pt, "h" => bin(hand), "ag" => c(agari) }
    w["pa"] = pao if pao
    w
  end

  TEN_YAKU = ["立直", "一発", "門前清自摸和", "断幺九", "平和", "一盃口", "三色同順", "混全帯幺九", "ドラ 4", "赤ドラ 2"].freeze
  TEN_HAN = [1, 1, 1, 1, 1, 1, 2, 2, 4, 2].freeze

  # MJ6: the game records as the host gives them (asterism-games README
  # "Game records", look's tile names). A list of twenty games of long
  # names; a game's head; a long round: every seat discards 22 times, a
  # riichi, a pon made an added kan, a closed kan, a chi, an open kan, kan
  # dora, three rons with ten yaku each.
  def replay_list
    (1..20).map do |i|
      { "id" => 21 - i, "at" => "2026-10-18 12:#{format('%02d', i)}", "game" => i.odd? ? "tonpuu" : "hanchan",
        "names" => [LONG_JA, LONG_EN, LONG_JA, LONG_EN], "ranks" => [1, 2, 3, 4].rotate(i), "scores" => [123_400, -12_300, 100_900, -112_000].rotate(i) }
    end
  end

  # MJ7: the players' stats as the host gives them (asterism-games README
  # "Stats"): thirty people of the longest names with the widest numbers,
  # and the CPU and AI players.
  def stats_rows
    rows = (1..30).map do |i|
      { "kind" => "human", "name" => i.odd? ? LONG_JA : LONG_EN, "games" => 1000 - i, "ranks" => [250, 250, 250, 249 - i],
        "rounds" => 99_999, "wins" => 22_222, "dealins" => 11_111, "riichi" => 18_888, "avg_score" => -123_456,
        "points" => -12_345.5 }
    end
    rows + [["cpu", "CPU"], ["claude", "Claude"], ["jev", "Jev"]].map do |k, n|
      { "kind" => k, "name" => n, "games" => 9999, "ranks" => [2500, 2500, 2500, 2499], "rounds" => 99_999, "wins" => 99_999,
        "dealins" => 0, "riichi" => 1, "avg_score" => 100_000, "points" => 9999.9 }
    end
  end

  def replay_head
    { "id" => 7, "at" => "2026-10-18 12:34", "game" => "hanchan", "names" => [LONG_JA, LONG_EN, LONG_JA, LONG_EN],
      "kinds" => %w[human cpu ai human], "final" => [[0, 1, 123_400, 1084], [2, 2, 100_900, 659], [1, 3, -12_300, -673], [3, 4, -112_000, -1070]],
      "reason" => "all_last_oya_tenpai", "rounds" => [["east", 1, 0, "ron_agari", [-96_000, 32_000, 32_000, 32_000]]] }
  end

  def replay_round
    pool = %w[1z 9m 2p 8s 3z 7p 4m 6s 5z 1p 9s 2m 6z 8p 3s 7m 4z 1s 9p 2s 7z 8m 3p 5s 6m 4p 1m 0m]
    moves = []
    k = 0
    22.times do |turn|
      4.times do |s|
        t = pool[k % pool.size]
        k += 1
        moves << [s, "draw", t]
        if s == 0 && turn == 5
          moves << [s, "riichi", t, true]
        else
          moves << [s, "discard", t, true]
        end
        if s == 0 && turn == 2
          moves << [1, "pon", t, [t, t], 0] << [1, "discard", "9s"]
        elsif s == 1 && turn == 8
          moves << [2, "draw", "7z"] << [2, "closed_kan", %w[7z 7z 7z 7z]] << [-1, "dora", "5p"] << [2, "draw", "3p"]
        elsif s == 2 && turn == 10
          moves << [3, "chi", t, %w[4m 6m], 2] << [3, "discard", "1z"]
        elsif s == 3 && turn == 12
          moves << [0, "open_kan", t, [t, t, t], 3] << [0, "draw", "2z"] << [0, "discard", "2z", true] << [-1, "dora", "6z"]
        elsif s == 1 && turn == 14
          moves << [1, "draw", "1z"] << [1, "added_kan", "1z"] << [1, "draw", "8m"]
        end
      end
    end
    moves << [1, "ron", "6m", 0] << [2, "ron", "6m", 0] << [3, "ron", "6m", 0]
    wins = [1, 2, 3].map do |w|
      { "seat" => w, "yaku" => TEN_YAKU, "yaku_han" => TEN_HAN, "han" => 16, "fu" => 30, "points" => "30符 16翻 数え役満 32000点",
        "hand" => %w[1m 2m 3m 4p 5p 6p 7s 8s 9s 1z 1z 2s 3s], "tile" => "6m" }
    end
    { "id" => 7, "round" => 0, "of" => 1, "part" => 0, "parts" => 1, "wind" => "west", "number" => 4, "honba" => 12,
      "riichi_sticks" => 9, "dealer" => 3, "scores" => [123_400, -12_300, 100_900, -112_000],
      "dora_indicators" => %w[1z 9m 5p], "hands" => [%w[1m 2m 3m 4m 5m 6m 7m 8m 9m 1p 2p 3p 4p]] * 4,
      "moves" => moves, "result" => { "kind" => "ron_agari", "changes" => [-96_000, 32_000, 32_000, 32_000], "from" => 0, "wins" => wins },
      "end_scores" => [27_400, 19_700, 132_900, -80_000] }
  end

  # The cases: name => [view, what it is].
  def all
    out = {}

    # 1. Four melds each, among them three or four kans; the own turn.
    v = base
    v["fu"] = four_kans
    v["hc"] = [2, 1, 1, 1]
    v["h"] = bin(%w[5m])
    v["dt"] = c("5m")
    v["a"] = [[1, bin(%w[5m])], [3, "".b], [5, bin(%w[1m])]]
    v["kw"] = [river(20, riichi: 5), river(21, start: 4), river(22, start: 8), river(23, start: 12)]
    v["ri"] = 0b0001
    out["melds_four_kans"] = [v, "四人とも 4 つの副露 (槓 3-4 つ、横の牌)、長い河、自分の手番"]

    # 2. Long rivers (24 each, a riichi tile in each), the calls: every
    # button a discard can give.
    v = base
    v["r"] = [1, 3, 5, 3, 1, 2, 3, 2]
    v["h"] = bin(%w[2m 3m 4m 4m 5m 6p 7p 8p 3s 3s 4s 5s 4z])
    v["dt"] = -1
    v["a"] = [[9, "".b], [6, bin(%w[4m 4m])], [8, "".b], [7, bin(%w[2m 3m])], [7, bin(%w[3m 5m])], [7, bin(%w[5m 6m])], [10, "".b]]
    v["dl"] = [1, 7_000]
    v["ls"] = [3, c("4m")]
    out["rivers_calls"] = [v, "河 24 枚ずつ (リーチの横の牌)、鳴きのボタン全部 (ロン・ポン・カン・チー 3 通り・パス)"]

    # 2b. The longest river at a turn with many buttons and the help (tenpai waits).
    v = base
    v["kw"] = [river(28, riichi: 9), river(30, riichi: 2, start: 3), river(29, riichi: 20, start: 7), river(31, riichi: 23, start: 11)]
    v["h"] = bin(%w[1m 1m 1m 2m 3m 4m 5m 6m 7m 8m 9m 9m 9m])
    v["dt"] = c("5m")
    v["a"] = [[1, bin(%w[1m 2m 3m 4m 5m 6m 7m 8m 9m])], [2, bin(%w[1m 2m 3m 4m 5m 6m 7m 8m 9m])], [3, "".b],
              [4, bin(%w[1m])], [4, bin(%w[9m])], [11, "".b]]
    v["wt"] = (%w[1m 2m 3m 4m 5m 6m 7m 8m 9m].flat_map { |d| [c(d), 13] + %w[1m 9m 1p 9p 1s 9s 1z 2z 3z 4z 5z 6z 7z].flat_map { |w| [c(w), 3] } }).pack("C*").b
    out["rivers_longest_turn"] = [v, "河 28-31 枚、自分の手番のボタン (リーチ・ツモ・カン 2・九種九牌)、待ち 13 種"]

    # 2c. Level 3 help: the acceptance of every kind.
    v = base
    v["h"] = bin(%w[1m 4m 7m 2p 5p 8p 3s 6s 9s 1z 2z 3z 4z])
    v["dt"] = c("5z")
    v["a"] = [[1, bin(%w[1m 4m 7m 2p 5p 8p 3s 6s 9s 1z 2z 3z 4z 5z])]]
    v["uk"] = (%w[1m 4m 7m 2p 5p 8p 3s 6s 9s 1z 2z 3z 4z 5z].flat_map do |d|
      [c(d), 6, 34] + (0..33).flat_map { |i| [i < 9 ? i : (i < 18 ? i + 1 : (i < 27 ? i + 2 : i + 3)), 4] }
    end).pack("C*").b
    out["assist_ukeire"] = [v, "アシスト 3: 打牌ごとに 34 種の受け入れ"]

    # 3. Three rons, each with ten yaku, melds in the winners' hands.
    v = base(st: "result")
    v["fu"] = four_kans
    v["hc"] = [1, 1, 1, 1]
    v.delete("dl")
    v["a"] = [[12, "".b]]
    wins = [1, 2, 3].map do |w|
      win(w, TEN_YAKU, TEN_HAN, "30符 16翻 数え役満 32000点", %w[6m], "6m")
    end
    v["res"] = { "k" => "ron_agari", "d" => [-96_000, 32_000, 32_000, 32_000], "rd" => [2, 4, 12], "f" => 0, "wn" => wins }
    out["result_triple_ron"] = [v, "三家和なしの 3 人の和了、役 10 個ずつ、副露 4 つずつ"]

    # 3b. The same three rons with closed hands (13 tiles each).
    v = v.dup
    v["fu"] = ["", "", "", ""]
    v["res"] = v["res"].merge("wn" => [1, 2, 3].map do |w|
      win(w, TEN_YAKU, TEN_HAN, "30符 16翻 数え役満 32000点", %w[1m 2m 3m 4p 5p 6p 7s 8s 9s 1z 1z 2s 3s], "4s")
    end)
    out["result_triple_ron_closed"] = [v, "3 人の和了、門前の 13 枚、役 10 個ずつ"]

    # 4. Several yakuman, a double yakuman and pao in one hand.
    v = base(st: "result")
    v.delete("dl")
    v["a"] = [[12, "".b]]
    v["fu"] = [melds([meld(2, 2, %w[5z 5z 5z]), meld(2, 1, %w[6z 6z 6z]), meld(2, 3, %w[7z 7z 7z])]), "", "", ""]
    v["hc"] = [4, 13, 13, 13]
    yaku = ["大三元", "字一色", "四暗刻単騎", "小四喜", "ドラ 3"]
    v["res"] = { "k" => "tsumo_agari", "d" => [192_000, -64_000, -64_000, -64_000], "rd" => [0, 1, 0],
                 "wn" => [win(0, yaku, [-1, -1, -2, -1, 3], "役満 5倍 192000点 (64000オール)", %w[1z 1z 2z 2z], "2z",
                              pao: [[1, "大三元"], [2, "小四喜"]])] }
    out["result_yakuman"] = [v, "役満の複合・ダブル役満・包 2 つ"]

    # 5. Nagashi mangan at a draw (two seats), everybody's tenpai marks.
    v = base(st: "result")
    v.delete("dl")
    v["a"] = [[12, "".b]]
    v["res"] = { "k" => "ryuukyoku", "d" => [8_000, 8_000, -8_000, -8_000], "rd" => [1, 4, 12], "tp" => [0, 2], "ng" => [1, 3] }
    out["result_nagashi"] = [v, "流し満貫 2 人の流局"]

    # 6. The abortive draws.
    %w[kyuushu_kyuuhai sanchahou suufon_renda suukaikan suucha_riichi].each do |k|
      v = base(st: "result")
      v.delete("dl")
      v["a"] = [[12, "".b]]
      v["res"] = { "k" => k, "d" => [0, 0, 0, 0], "rd" => [0, 1, 0] }
      v["res"]["rs"] = [1, 2, 3] if k == "sanchahou"
      out["result_#{k}"] = [v, "途中流局 (#{k})"]
    end

    # 7. The end of the game.
    v = base(st: "over")
    v.delete("dl")
    v["a"] = [[12, "".b]]
    v["res"] = { "k" => "tsumo_agari", "d" => [36_000, -12_000, -12_000, -12_000], "rd" => [2, 4, 12],
                 "wn" => [win(0, TEN_YAKU, TEN_HAN, "16翻 数え役満 48000点 (16000オール)", %w[1m 2m 3m 4p 5p 6p 7s 8s 9s 1z 1z 2s 3s], "4s")] }
    v["fin"] = [[0, 1, 123_400, 1084], [2, 2, 100_900, 659], [1, 3, -12_300, -673], [3, 4, -112_000, -1070]]
    v["fr"] = "all_last_oya_tenpai"
    out["over"] = [v, "終局 (6 桁と負の点、長い名前、理由)"]

    # 8. The spectators' screen: the four hands.
    v = base(me: -1)
    v["fu"] = [melds([meld(1, 3, %w[3m 4m 5m]), meld(5, 2, %w[6p 6p 6p 6p])]), four_kans[1], "",
               melds([meld(2, 1, %w[7s 7s 7s])])]
    v["hc"] = [8, 1, 13, 11]
    v["sh"] = [bin(%w[1m 2m 3m 7p 8p 9p 5z 5z]), bin(%w[9m]), bin(%w[1m 1m 2p 3p 4p 0p 6s 7s 8s 2z 2z 3z 3z]),
               bin(%w[2m 3m 4m 0m 6m 7m 1p 2p 3p 9s 9s])]
    v["sd"] = [c("4z"), -1, -1, -1]
    v["r"] = [0, 2, 1, 0, 0, 30, 0, 1]
    v["a"] = []
    v.delete("dl")
    out["spectator"] = [v, "観戦: 4 人の手牌 (副露あり、手番の席に自摸の牌)"]

    v = base(me: -1)
    v["fu"] = four_kans
    v["hc"] = [2, 1, 1, 1]
    v["sh"] = [bin(%w[9p]), bin(%w[9m]), bin(%w[1s]), bin(%w[7z])]
    v["sd"] = [c("9p"), -1, -1, -1]
    v["a"] = []
    v.delete("dl")
    out["spectator_four_kans"] = [v, "観戦: 四人とも槓 4 つ、河 24 枚"]

    v = base(me: -1)
    v["sh"] = [bin(%w[1m 2m 3m 4m 5m 6m 7m 8m 9m 1p 2p 3p 4p]), bin(%w[1s 2s 3s 4s 5s 6s 7s 8s 9s 1z 2z 3z 4z]),
               bin(%w[1m 2m 3m 4m 5m 6m 7m 8m 9m 1p 2p 3p 4p]), bin(%w[5z 6z 7z 1z 2z 3z 4z 5z 6z 7z 1s 2s 3s])]
    v["sd"] = [c("5p"), -1, -1, -1]
    v["a"] = []
    v.delete("dl")
    out["spectator_closed"] = [v, "観戦: 4 人とも門前 13 枚と自摸"]

    # MJ8: the guide. Another seat's turn (the longest name), and the
    # others answering a discard (who may call is not told).
    v = base
    v["r"] = [2, 4, 12, 9, 3, 7, 1, 1]
    v["h"] = bin(%w[2m 3m 4m 4m 5m 6p 7p 8p 3s 3s 4s 5s 4z])
    v["dt"] = -1
    v["a"] = []
    v["dl"] = [0b0010, 9_000]
    out["guide_other_turn"] = [v, "ほかの人の番 (長い名前の席の番)"]
    v = v.dup
    v["r"] = [2, 4, 12, 9, 3, 7, 0, 2]
    v["dl"] = [0b1110, 9_000]
    out["guide_others_answer"] = [v, "鳴きの答え待ち (自分は答えない)"]

    # MJ8: the same tiles. A red five chosen: fives (red or not) in every
    # river, in melds of every seat (a pon, a chi, an added kan) and among
    # the dora indicators.
    v = base
    v["h"] = bin(%w[1m 2m 3m 0m 5m 6p 7p 8p 3s 3s 4s 5s 4z])
    v["dt"] = c("5m")
    v["a"] = [[1, bin(%w[1m 2m 3m 0m 5m 6p 7p 8p 3s 3s 4s 5s 4z])]]
    v["kw"] = [bin(%w[1z 5m 9p 0m 2s 7z]), bin(%w[5m 3p 4s]), bin(%w[2z 0m 8m 5m]), bin(%w[9s 5m])]
    v["fu"] = ["", melds([meld(2, 3, %w[5m 5m 5m])]), melds([meld(1, 3, %w[4m 5m 6m]), meld(5, 1, %w[5m 5m 5m 0m])]),
               melds([meld(1, 3, %w[3m 4m 0m])])]
    v["hc"] = [13, 10, 7, 10]
    v["dora"] = bin(%w[4m 0m 1z])
    out["marks_red_five"] = [v, "赤五を選んだときの同じ牌の印 (河・3 人の鳴き・ドラ表示牌)"]

    # 9. The lobby with the most members and the rules, owner (both pages).
    v = base(st: "wait")
    %w[r sc ri dora kw fu hc ls ev dl].each { |k| v.delete(k) }
    v["tc"] = [20_005, 5, 30, 60, 600]
    v["dc"] = [0, 60, 180, 300]
    v["ry"] = [2, 300]
    out["lobby_owner"] = [v, "待ち合わせ: 16 人、長い名前、卓の主のルールの欄"]
    v = v.dup
    v["me"] = 1
    out["lobby_member"] = [v, "待ち合わせ: 卓の主でない人"]
    out
  end
end
