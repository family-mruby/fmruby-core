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

Style rules these samples follow (from `doc/fmrb_basic/spec/`):

- no `LET` (Family BASIC has no such keyword; assignment is `A=10`)
- `NEXT` carries no loop variable name
- integers only, -32768 - 32767; `/` truncates
- string variables hold at most 31 characters
- no lower case: the character set has upper case only, and the loader folds
  lower case source characters to upper case

Screen (B2) and game (B3) samples are added by those phases.
