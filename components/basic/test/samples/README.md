# BASIC samples (original code)

Hand written Family BASIC V3 programs used as an implementation target and,
once the matching feature set is implemented, as golden test material. They are
original code written for this project; no third party listing is stored here.

A sample is promoted into `test/golden/` (with an `.expected` file) by the
phase that makes it runnable, and disappears from this directory.

| Sample | Feature range | Promoted in |
|---|---|---|
| `sample_01_fizzbuzz.bas` | FOR/NEXT, MOD, IF-THEN line branch, GOTO, PRINT | B1 -> `golden/160_sample_fizzbuzz` |
| `sample_02_strings.bas` | string variables, concatenation, LEN/LEFT$/RIGHT$/MID$/ASC/CHR$/VAL/STR$ | B1 -> `golden/161_sample_strings` |
| `sample_03_data_array.bas` | DIM, DATA/READ/RESTORE, GOSUB/RETURN, integer division | B1 -> `golden/162_sample_data_array` |
| `sample_04_screen_kana.bas` | kana PRINT, COLOR, LOCATE, blocking INKEY$(0), kana input mode | stays here: it waits for key presses, so it is run by hand in the simulation |
| `sample_10_dodge.bas` | DEF MOVE 8-direction control via STICK table, redefine-and-reposition idiom (XPOS/YPOS + POSITION + MOVE), CRASH, ERA | B3 bring-up corpus |
| `sample_11_shoot.bas` | three concurrent MOVEs, STRIG fire, CRASH between two moving sprites, CAN, PLAY jingle, frame-count timer | B3 bring-up corpus |
| `sample_12_maze.bas` | SCR$ collision on the text screen, COLOR, STICK or IJKM keys, kana messages, random layout | B2/B3 bring-up corpus |
| `sample_13_music.bas` | PLAY 3 channels (T/M/Y/V/O/R) under the 31-char string limit, asynchronous playback while the screen animates, CLICK, BEEP | B3 bring-up corpus |
| `sample_14_hit.bas` | reaction game: RND, PAUSE-based timeout, INKEY$ polling, kana, score ranking | B2/B3 bring-up corpus |

The `sample_1x` games are the substitute target corpus for B3 T3-7 until the
user supplies real-world listings. They intentionally lean on behaviors the
spec leaves thin, so bring-up will surface questions early. Known probes:

- `sample_10/11`: MOVE(n) on a defined-but-never-moved motion (expected 0),
  position retention across DEF MOVE redefinition, CAN followed by
  POSITION + MOVE without redefining.
- `sample_13`: whether a second PLAY while the first is still sounding
  queues, restarts, or waits (spec is silent; the sample paces phrases with
  PAUSE so any of the three behaviors still sounds acceptable). Record the
  implemented choice in the B3 report's question list.

Style rules these samples follow (from `doc/fmrb_basic/spec/`):

- no `LET` (Family BASIC has no such keyword; assignment is `A=10`)
- `NEXT` carries no loop variable name
- integers only, -32768 - 32767; `/` truncates
- string variables hold at most 31 characters
- no lower case: the character set has upper case only, and the loader folds
  lower case source characters to upper case

Game (B3) samples are added by that phase.

Running a sample by hand in the Linux simulation (the stack must be up, see
the family-mruby root CLAUDE.md):

```
cp components/basic/test/samples/sample_04_screen_kana.bas flash/app/demo/_try.app.bas
printf 'app_handle_name = "_try"\napp_screen_name = "try"\n' > flash/app/demo/_try.app.toml
python3 - <<'EOF'
import sys; sys.path.insert(0, "tool/debug")
from fmrb_dbg_client import FmrbDebugClient, TcpTransport
c = FmrbDebugClient(TcpTransport("127.0.0.1", 5555)); c.connect()
print(c.spawn("/app/demo/_try.app.bas")); c.close()
EOF
# click once on the text plane to give it the keyboard, then type
python3 ../tools/fmrb_input.py click 150 100 sleep 500 text "AB" key tab text "KANA"
python3 ../tools/fmrb_screenshot.py /tmp/try.png
```
