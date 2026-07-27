# BASIC samples (original code)

Hand written Family BASIC V3 programs used as an implementation target and,
once the matching feature set is implemented, as golden test material. They are
original code written for this project; no third party listing is stored here.

Phase B0 keeps them out of `test/golden/` because the interpreter core cannot
execute statements yet: adding them as golden cases would only pin down failure
output. Each phase promotes the samples it makes runnable:

| Sample | Feature range | Promoted in |
|---|---|---|
| `sample_01_fizzbuzz.bas` | FOR/NEXT, MOD, IF-THEN line branch, GOTO, PRINT | B1 |
| `sample_02_strings.bas` | string variables, concatenation, LEN/LEFT$/RIGHT$/MID$/ASC/CHR$/VAL/STR$ | B1 |
| `sample_03_data_array.bas` | DIM, DATA/READ/RESTORE, GOSUB/RETURN, integer division | B1 |

Style rules these samples follow (from `doc/fmrb_basic/spec/`):

- no `LET` (Family BASIC has no such keyword; assignment is `A=10`)
- `NEXT` carries no loop variable name
- integers only, -32768 - 32767; `/` truncates
- string variables hold at most 31 characters

Screen (B2) and game (B3) samples are added by those phases.
