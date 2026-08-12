# Editor string registrations (doc/editor_ja/plan.md 3.4).
#
# FmrbI18n itself lives in picoruby-fmrb-app/mrblib/fmrb-i18n.rb, but its
# STRINGS table is per-VM: the desktop's registrations are not visible here, so
# the editor registers everything it draws, including words the desktop also
# has (save, cancel, close).
#
# Accelerator letters are NOT in here. They come from the MENU_*_HOTKEYS
# scancode tables and stay the same in every language; the label just shows
# them in parentheses, as in "File(F)" / "ファイル(F)".

# NB: the argument is an explicit braced Hash. Written as bare `"en" => {...}`
# pairs, Spinel reads them as keyword arguments and hands `add` a symbol-keyed
# hash, so the "en"/"ja" lookup inside finds nothing and every string comes back
# as its own key name. Same meaning in mruby either way.

# Built at load, i.e. in the Spinel program's entry. That was briefly moved to
# on_create to work around a generator bug (a second instance of the program
# started with the previous one's pointers in its constant statics, and a
# collection during the entry marked them); the generator now clears those
# statics per instance, so this is back where it belongs -- and being in the
# entry means every editor launch exercises the fix.
# See doc/spinel_aot/report/stale_statics.md.
FmrbI18n.add({
  "en" => {
    # Menu bar
    m_file:      "File",
    m_edit:      "Edit",
    m_search:    "Search",
    m_run:       "Run",
    m_hilight:   "Hilight",
    m_wrap:      "Wrap",
    m_debug:     "Debug",
    m_full:      "Full",
    # File menu
    open:        "Open",
    save:        "Save",
    save_as:     "Save as",
    template:    "Template",
    exit:        "Exit",
    # Edit menu
    cut:         "Cut",
    copy:        "Copy",
    paste:       "Paste",
    select_all:  "Select All",
    # Search dialog
    find:        "Find:",
    find_keys:   "[Enter]Find  [F3]Next  [Esc]Cancel",
    not_found:   "Not found",
    # Quit confirmation
    unsaved:     "Unsaved changes",
    save_before_exit: "Save before exit?",
    q_yes:       "es",
    q_no:        "o",
    q_cancel:    "ancel/Esc",
    # Status line
    st_new:      "[New]",
    st_ln:       "Ln",
    st_col:      "Col",
    st_hl_off:   "[HL off]",
    # Status badges
    b_saved:     "Saved",
    b_save_failed: "Save failed",
    b_load_failed: "Load failed",
    b_too_large: "Too large",
    b_doc_full:  "Doc full",
    b_empty:     "Empty",
    b_inserted:  "Inserted",
    b_no_templates: "No templates",
    b_running:   "Running",
    # Completion (Tab)
    b_comp_none: "No suggestions",
    b_comp_too_large: "File too big to suggest",
    # Type information (Ctrl+T) and diagnostics (Ctrl+E)
    b_no_type:   "No type here",
    b_no_help:   "No help for that yet",
    b_readonly:  "Help page: read only",
    b_no_problems: "no problems",
    b_problem:   "problem",
    b_problems:  "problems",
    b_diag_failed: "Could not check",
    b_run_failed: "Run failed",
    b_run_path:  "Run: need a path",
    b_run_pid:   "Run pid",
  },
  "ja" => {
    m_file:      "ファイル",
    m_edit:      "編集",
    m_search:    "検索",
    m_run:       "実行",
    m_hilight:   "色分け",
    m_wrap:      "折返し",
    m_debug:     "デバッグ",
    m_full:      "全画面",
    open:        "開く",
    save:        "保存",
    save_as:     "名前を付けて保存",
    template:    "ひな形",
    exit:        "終了",
    cut:         "切り取り",
    copy:        "コピー",
    paste:       "貼り付け",
    select_all:  "すべて選択",
    find:        "検索:",
    find_keys:   "[Enter]検索 [F3]次 [Esc]中止",
    not_found:   "見つかりません",
    unsaved:     "変更があります",
    save_before_exit: "保存しますか?",
    q_yes:       "はい",
    q_no:        "いいえ",
    q_cancel:    "中止/Esc",
    st_new:      "[新規]",
    st_ln:       "行",
    st_col:      "桁",
    st_hl_off:   "[色分けなし]",
    b_saved:     "保存しました",
    b_save_failed: "保存できません",
    b_load_failed: "読み込めません",
    b_too_large: "大きすぎます",
    b_doc_full:  "容量が一杯です",
    b_empty:     "空です",
    b_inserted:  "挿入しました",
    b_no_templates: "ひな形がありません",
    b_running:   "実行中",
    b_comp_none: "候補がありません",
    b_comp_too_large: "大きすぎて候補を出せません",
    b_no_type:   "ここには型がありません",
    b_no_help:   "まだ説明がありません",
    b_readonly:  "説明の画面です (書けません)",
    b_no_problems: "問題なし",
    b_problem:   "件の問題",
    b_problems:  "件の問題",
    b_diag_failed: "調べられませんでした",
    b_run_failed: "実行できません",
    b_run_path:  "パスが必要です",
    b_run_pid:   "実行 pid",
  }
})
