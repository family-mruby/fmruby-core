# Kanji conversion

In kana input (Ctrl+Space, or Hankaku/Zenkaku on a JIS keyboard) you can
turn a reading into kanji, one word at a time. It works once the dictionary
file is on the SD card. Without it, kana input is as it always was.

The browser build and the native (Linux) build do not use this dictionary;
see "The browser and native builds" below.

## 1. Getting the dictionary

- Two files attached to the Family mruby release (on its GitHub release
  page):
  - fmrb_kanji.dic            the dictionary (about 1 MB)
  - fmrb_kanji_LICENSE.txt    the terms of the data it is made from
- Developers can make it from the source: `rake kanji:dict` in fmruby-core
  fetches the source data and writes the dictionary and its LICENSE to
  tools/kanji/work/out/. The same source tree makes the same file as the
  release's.

## 2. Putting it on the SD card

The machine reads /sd/dict/fmrb_kanji.dic (a folder named dict at the top
of the card). Keep the LICENSE in the same folder.

### Copying on a PC

1. Put the card in the PC and make a folder dict at its top.
2. Copy fmrb_kanji.dic and fmrb_kanji_LICENSE.txt into it.
3. Put the card back and restart the machine.

Developers can use `rake kanji:place DEST=<the card's top>`, which makes
the dict folder and checks that the copy is whole.

### Sending it over the network (Asterism)

From the Shell of another Family mruby machine:

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
with -o to overwrite instead). Restart the machine after moving it.

The machine looks for the dictionary **when it starts**, so restart after
putting or replacing it. With no card, no dictionary or a broken one, kana
input works as before, without conversion.

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
  to the app. Turning kana mode off or moving to another app commits what
  is shown too.
- The Enter that commits does not make a new line.
- When the whole reading is not in the dictionary, the longest leading part
  that is gets converted, and the rest becomes the next reading
  (いった -> 行っ, then た).
- A single kanji converts from its reading as well: every kanji the
  machine's font has can be typed.

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
  most common words go in up to 1 MB (about 80,000 words).
- It does not learn: candidates always come in the same order.
- Words whose readings kana input cannot type (てぃ, ふぁ, ゔ ...) are not
  in it.
