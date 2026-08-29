# ruler: half-width kana vs full-width, for the cell-width check
# 0123456789012345678901234567890
# ｱｲｳｴｵ|
# あいうえお|いうえ
# ABCDE|
# 日本語ひょうじの てすと (doc/editor_ja/instruction_ja1.md T2)
# かんじ・ひらがな・カタカナ・ﾊﾝｶｸｶﾅ・ASCII が まざった ぎょう。
class ニホンゴApp
  MESSAGE = "こんにちは、せかい"   # ぜんかく の もじれつ
  KANA = "ｱｲｳｴｵ ｶｷｸｹｺ"           # はんかくカナ は 1 セル

  def あいさつ
    # 60 もじ いじょう の ながい ぎょう: ぎょうまつ まで えがかれる はず。
    puts "あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん"
  end

  def mixed_line
    x = 1  # ここに にほんご の コメント。ASCII と まざる。
    puts "abc あいう def かきく ghi さしす jkl たちつ mno"
    x
  end
end

begin
  ニホンゴApp.new.あいさつ
rescue => e
  Log.error("JA: #{e.class}: #{e.message}")
end