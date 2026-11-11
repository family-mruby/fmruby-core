# Kanji conversion

In kana input (Ctrl+Space, or Hankaku/Zenkaku on a JIS keyboard) you can
turn a reading into kanji, one word at a time. It works once the dictionary
file is on the SD card. Without it, kana input is as it always was.

The browser build and the native (Linux) build do not use this dictionary;
see "The browser and native builds" below.

## 1. Getting the dictionary

- Files attached to the Family mruby release (on its GitHub release
  page):
  - fmrb_kanji.dic            the dictionary (1 MB, the standard one)
  - fmrb_kanji_4m.dic         a larger dictionary (4 MB, about 340,000
                              words), for an SD card
  - fmrb_kanji_LICENSE.txt    the terms of the data they are made from
- Developers can make them from the source: `rake kanji:dict_release` in
  fmruby-core fetches the source data and writes both dictionaries and the
  LICENSE to tools/kanji/work/out/ (`rake kanji:dict` makes the 1 MB one
  only). The same source tree makes the same files as the release's.

## 2. Putting it on the SD card

The machine reads /sd/dict/fmrb_kanji.dic (a folder named dict at the top
of the card). Keep the LICENSE in the same folder.

To use the larger dictionary, rename fmrb_kanji_4m.dic to fmrb_kanji.dic
when you put it there, or keep its name and point kanji_dict at it (section
4). The machine uses one dictionary.

### Copying on a PC

1. Put the card in the PC and make a folder dict at its top.
2. Copy fmrb_kanji.dic and fmrb_kanji_LICENSE.txt into it.
3. Put the card back and restart the machine.

Developers can use `rake kanji:place DEST=<the card's top>`, which makes
the dict folder and checks that the copy is whole (it copies the 4 MB one
too when it was made).

### Sending it over the network (Asterism)

From the Shell of another Family mruby machine (`anodes` lists the
machines' node IDs):

    asend <node> fmrb_kanji.dic --sd

From a PC, `ruby tools/fmrb_send.rb <node> fmrb_kanji.dic --sd` (in
fmruby-core); the console's page can send to the SD card too. The receiving
machine must be receiving first (`areceive` in its Shell, or the Receiving
app).

**A file sent with --sd lands in /sd/inbox/.** In the receiving machine's
Shell, move it into dict:

    mkdir /sd/dict
    mv /sd/inbox/fmrb_kanji.dic /sd/dict/

Skip the mkdir when /sd/dict/ is already there. If the inbox already has a
file of that name, the new one gets a name like fmrb_kanji(1).dic (send
with -o to overwrite instead).

The machine **uses the dictionary as soon as it is placed or replaced** (no
restart). While it is being replaced, and after it is deleted, kana input
goes on without conversion; so it does with no card, no dictionary or a
broken one. Where there is no room for two dictionaries, delete the old one
first, then put the new one.

## 3. Using it

In kana mode, a reading you type collects in a line at the bottom of the
screen (it does not reach the app yet).

| Key | While typing a reading | While candidates are shown |
|---|---|---|
| Space, Henkan | convert: show candidates | next candidate |
| Enter | commit the kana as they are | commit the selected candidate |
| 1-9 | commit the kana, then type the digit | commit that numbered candidate |
| Esc | discard the reading | back to the reading |
| BS | delete one character | back to the reading, one character less |
| more kana | add to the reading | commit the candidate, start the next reading |

- Arrows, Tab, symbols, Ctrl+S and the like commit what is shown, then go
  to the app. Turning kana mode off commits what is shown too. Moving to
  another app (a click on another window) discards the reading.
- The Enter that commits does not make a new line.
- When the whole reading is not in the dictionary, the longest leading part
  that is gets converted, and the rest becomes the next reading
  (いった -> 行っ, then た).
- A single kanji converts from its reading as well: every kanji the
  machine's font has can be typed.
- The katakana form of what is converted is always among the candidates:
  second, or first when the dictionary has nothing for it.
- When only a leading part matches, the katakana of the whole reading comes
  second and that of the part third (ふぁいる -> 1 華 2 ファイル 3 ファ ...).
  Picking the whole one replaces all of the reading; the other candidates
  replace the matched part and leave the rest as the next reading. On the
  line, the part of the reading the selected candidate replaces is bright
  and the rest dimmed.

### The line at the bottom

The reading on the left, the numbered candidates after it, and
"current/total" at the right end. The selected candidate is shown inverted.
With many candidates, Space moves on to the next page (after the last, back
to the first). "(候補なし)" means there are none. The line is in the same
place over every app, full-screen ones included.

## 4. Turning it off, or another place

Write these lines in /etc/system_conf.toml and restart:

    kanji_convert = false                    # no conversion, file or not
    kanji_dict = "/sd/dict/fmrb_kanji.dic"   # where the dictionary is (the default)

Taking the dictionary off the card turns conversion off too.

## 5. The browser and native builds

- Browser build: it uses the browser's (the PC's) Japanese input method
  (IME). In an app with a text cursor (the editor, the Shell), switch the
  IME to Japanese and type; the committed text goes into the app. Apps
  without a cursor (games) do not get the IME.
- Native build (Linux): it uses the Linux input method.

Neither needs this dictionary.

## 6. About the dictionary

- It is made from mozc's dictionary data (IPAdic and others) and the
  Japanese readings of Unicode's Unihan. The terms are in
  fmrb_kanji_LICENSE.txt.
- Candidates are only words whose characters the machine's font draws. The
  most common words go in up to 1 MB (about 79,000 words), or 4 MB (about
  340,000) for the larger one.
- It does not learn: candidates always come in the same order.
- Words that are just their reading in katakana (センセイ) are not in it:
  the katakana form is added to the candidates instead.

## 7. Small kana and sounds of loanwords

Kana input takes the spellings the common input methods take:

| Type | Get |
|---|---|
| xa xi xu xe xo (or la ...) | ぁ ぃ ぅ ぇ ぉ |
| xtu xtsu (or ltu ltsu) | っ |
| xya xyu xyo (or lya ...), xwa | ゃ ゅ ょ, ゎ |
| xka xke | ヵ ヶ |
| tha thi thu the tho, dha ... | てゃ てぃ てゅ てぇ てょ, でゃ ... |
| twa ... two, dwa ... dwo | とぁ ... とぉ, どぁ ... どぉ |
| fa fi fe fo, fya fyu fyo | ふぁ ふぃ ふぇ ふぉ, ふゃ ふゅ ふょ |
| va vi vu ve vo, vya vyu vyo | ゔぁ ゔぃ ゔ ゔぇ ゔぉ, ゔゃ ゔゅ ゔょ |
| tsa tsi tse tso | つぁ つぃ つぇ つぉ |
| wi we, wha whi whe who | うぃ うぇ, うぁ うぃ うぇ うぉ |
| kwa ... kwo, gwa ... gwo, qa qi qe qo | くぁ ... くぉ, ぐぁ ... ぐぉ, くぁ くぃ くぇ くぉ |

In katakana mode they come out in katakana. An x, l, v or q that does not
start one of these is typed as the letter, as before.
