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
