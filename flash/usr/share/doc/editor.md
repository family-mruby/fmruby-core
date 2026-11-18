# The editor: several files at once

The editor keeps up to five files open. While there is more than one, a row
of tabs under the menu bar names them, on a band of its own colour between
the menu bar and the text; the one you are editing is shown inverted, and a
"*" after a name means it has changes you have not saved. With a single file
the row is not there.

Opening and closing

- File > Open opens the file beside the ones already open. If the file is
  already open, the editor just switches to it. If the file you are on is
  new and empty, the file opens in its place.
- File > New (Ctrl+N) adds an empty file.
- File > Template puts the skeleton into a new file, or into the file you
  are on when that one is new and empty.
- File > Close (Ctrl+W) closes the file you are on. If it has unsaved
  changes, the editor asks first: Y saves it, N throws the changes away,
  C or Esc keeps it open. Closing the last file leaves an empty one.
- With five files open, opening another is refused; close one first.

Opening from elsewhere

While the editor runs, a file opened from the file manager or with the
shell's edit goes to the running editor as a new tab (the File > Open rules
apply) instead of starting a second editor. If a question or a menu is up,
the file opens once it is gone. The editor's icon and menu entry on the
desktop bring the running editor to the front.

Moving between files

- Click a tab.
- Alt+. and Alt+, go to the next and the previous file.
- Alt+1 to Alt+5 go to the file in that place.
- Ctrl+PageDown and Ctrl+PageUp do the same as Alt+. and Alt+, (for a
  keyboard without Alt).

Leaving the editor

Ctrl+X, Ctrl+Q and File > Exit ask about every file with unsaved changes,
one after another, showing each file before its question. C or Esc stops
there and keeps the editor open.

Run (F5), Search, the type help (F1, Ctrl+T, Ctrl+E) and the clipboard work
on the file you are on. Copy in one file and paste in another to move text
between them. The key list (Keys on the menu bar, or Alt+K) has all of this.

The editor remembers, per file, which app it last ran as with F5; the AI
Helper's Fix (Modern) uses that app's error only.
