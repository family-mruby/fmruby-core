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

# Registered from EditorApp#on_create rather than at load, and that placement is
# load-bearing on the Spinel build.
#
# A generated Spinel program keeps its constants in process-global C statics.
# The program entry re-assigns them in source order, but it installs the GC's
# globals-mark hook before the first assignment -- so a collection that happens
# while the entry is still running marks whatever the PREVIOUS instance of the
# program left in the statics further down the file. Those are pointers into a
# pool that has since been handed to a new heap, and marking them segfaults.
#
# Building this table is by far the largest allocation in the entry, so doing it
# there was what tipped the heap over the collection threshold and crashed the
# second time the editor was opened. By on_create the entry has finished and
# every constant holds a pointer from this instance. (The real fix is for the
# generator to reset those statics per instance; this keeps the editor usable
# until then.)
module EditorStrings
  def self.install
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
        b_run_failed: "実行できません",
        b_run_path:  "パスが必要です",
        b_run_pid:   "実行 pid",
      }
    })
  end
end
