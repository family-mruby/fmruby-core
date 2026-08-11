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
FmrbI18n.add({
  "en" => {
    # Menu bar
    m_file:      "File",
    m_edit:      "Edit",
    m_search:    "Search",
    m_run:       "Run",
    m_hilight:   "Hilight",
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
    b_run_failed: "Run failed",
    b_run_path:  "Run: need a path",
    b_run_pid:   "Run pid",
  },
  "ja" => {
    m_file:      "ファイル",
    m_edit:      "へんしゅう",
    m_search:    "さがす",
    m_run:       "じっこう",
    m_hilight:   "いろ",
    m_debug:     "デバッグ",
    m_full:      "ぜんめん",
    open:        "ひらく",
    save:        "ほぞん",
    save_as:     "なまえをつけて ほぞん",
    template:    "ひな形",
    exit:        "おわる",
    cut:         "きりとり",
    copy:        "コピー",
    paste:       "はりつけ",
    select_all:  "ぜんぶ えらぶ",
    find:        "さがす:",
    find_keys:   "[Enter]さがす [F3]つぎ [Esc]やめる",
    not_found:   "みつかりません",
    unsaved:     "ほぞんしていません",
    save_before_exit: "ほぞんしますか?",
    q_yes:       "はい",
    q_no:        "いいえ",
    q_cancel:    "やめる/Esc",
    st_new:      "[新規]",
    st_ln:       "行",
    st_col:      "桁",
    st_hl_off:   "[いろ なし]",
    b_saved:     "ほぞんしました",
    b_save_failed: "ほぞん できません",
    b_load_failed: "よみこめません",
    b_too_large: "大きすぎます",
    b_doc_full:  "いっぱいです",
    b_empty:     "からっぽ",
    b_inserted:  "いれました",
    b_no_templates: "ひな形がありません",
    b_running:   "じっこう中",
    b_run_failed: "じっこう できません",
    b_run_path:  "パスが ひつよう",
    b_run_pid:   "じっこう pid",
  }
})
