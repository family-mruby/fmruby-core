# 置き場の名前の変更の対応表

> 状態: 完了 | 更新: 2026-11-15 | doc/codebase_tidy の T6 (2026-11-15) で置き場の名前を変えた。今の文書・CLAUDE.md・skill・memory は新しいパスに直したが、doc/archive/ と各企画の report/ は当時の記録なので古いパスのまま。読むときはこの表で読み替える

| 古いパス | 新しいパス |
|---|---|
| `fmruby-core/tool/` | `fmruby-core/tools/` (ルートの `tools/` とは別) |
| `main/prebuild_scripts/` | `main/ruby/` |
| `lib/add/<自作 gem>` (picoruby-fmrb-* など 23 本) | `lib/gems/` |
| `lib/add/<上流への差し込み・設定・pin>` (`*_PIN`、`family_mruby*.rb`・gembox、hal-*、*-proxy など) | `lib/upstream/` |
| `lib/patch/`・`lib/replace/` | `lib/upstream/patch/`・`lib/upstream/replace/` |
| `config/*_n16r8*` (NARYA v3、S3 Retro) | `config/*_naryav3*` |
| `config/sdkconfig.defaults.p4`・`system_conf_p4.toml` (Tab5) | `config/sdkconfig.defaults.tab5`・`system_conf_tab5.toml` |
| `config/partitions_p4.csv` (Tab5 と NARYA v4 で共有) | `config/partitions_p4_common.csv` |
| `config/*_n8r8*` (ATOM_DISPLAY) | `config/*_atom_display*` |

- 置き換えの規則の全部は doc/codebase_tidy/report/t6.md。
