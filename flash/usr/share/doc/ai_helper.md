# AI Helper: making apps with an AI to ask

The AI Helper (/app/modern/ai_helper.app.rb) asks an AI about programs. The
answers come from an AI, not a person. The AI runs on a PC your family set
up (the tutor); this machine talks only to that tutor and holds no key for
any AI service. The window is in Japanese; the names below are its
buttons in English.

Uses (the top row)

- Question: type it on the bottom line and press Enter or Send. The editor's
  document is not changed. When the open document has something in it, it
  goes along with the question (the default). Press "Send the document" on
  the bottom row to make it "Don't send the document" and send the question
  alone. With no editor running, an empty document, or a dialog or help page
  up in the editor, the question goes alone. Your line says which:
  "(question + document name)" or "(question, no document)", and so does the
  status line.
- Hint: sends the open document and gets hints put in as comments starting
  with "# AI: " above the lines. No line of code changes. The text may be
  empty; saying what puzzles you aims the hints there.
- Make: describe an app and the AI writes the whole of it. Before it goes in
  you are asked whether to keep the current content: yes keeps it beside the
  file as "name.bak1" and so on (five at most; the first, .bak1, always
  stays), no replaces it without a copy, stop changes nothing. A document
  with no name cannot be kept, so you are asked whether to overwrite it.
- Fix: after an app run with F5 has failed, ask with its error. Only the
  error of the app the editor's current document last ran as with F5 is
  used (none for a document not run yet, or after switching to another). Each time
  you choose a hint or the fixed document (asked like Make before it goes in).
- Erase AI lines: counts the lines starting with "# AI: ", asks, and removes
  them. Lines you wrote in the same form go too.

Other buttons and keys

- Stop (Esc): stop what was asked, also while waiting for an answer.
- New talk: the earlier exchanges are no longer sent (the AI remembers the
  last few).
- Up/Down, PageUp/PageDown and the wheel scroll the answers.
- Kana and kanji: the usual kana input (Ctrl+Space).

Good to know

- What goes in is not saved yet: save it in the editor if you like it.
- Editing while you wait is fine: hints are placed again on the new text and
  Make asks again. Nothing is overwritten silently.
- Nothing goes in while the editor shows a dialog, a menu or help (F1): close
  it and press "Again".
- Closing this app while waiting is fine: open it again to get the answer.
- When something is in the way (the network, no tutor, today's limit used, a
  use turned off) the reason is shown on the bottom line.

For the family: the tutor is fmrb-tutor (doc/ai_coding). Its settings decide
which machines may ask, how many times a day, and whether Make and Fix are
allowed. Every exchange is recorded on the tutor's side.
