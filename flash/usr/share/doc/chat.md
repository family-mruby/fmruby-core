# Chat: talking in rooms with your family and friends

Chat (/app/modern/chat.app.rb) lets a set group of people talk in rooms, in
text. The messages are kept by a chat host your family set up, so you can
read on from where you were when you open it later. The window is in
Japanese; the names below are its words in English.

First

- The first time, it asks for your display name (up to 8 characters, or what
  the chat host allows). Type it and press Enter or "OK". Change it later with
  "Name" at the top right.
- Your display name is shown with your messages. When someone on another
  machine uses the same name, the last 3 characters of the machine's name are
  added after it, only then (e.g. Mom(a1b)).

The rooms

- Each room's name, and how many messages you have not read yet (3). Click a
  room (or Up/Down and Enter) to go in.
- Unread counts start from the last message this machine showed you, and are
  kept when the app is closed.
- The rooms are set on the chat host; a machine cannot make one.

In a room

- Type on the bottom line and press Enter or "Send". Kana and kanji work
  with the usual kana input (Ctrl+Space); in the browser and on a Linux PC
  the PC's input method (IME) works too.
- A message reads "month/day time name: text" (no year); yours are in another colour. The time
  is the chat host's clock.
- Read back with the arrows, PageUp, PageDown or the wheel. Going up past the
  top fetches older messages. A machine keeps up to 50 messages of a room at
  a time (farther ones are fetched again).
- Esc or "<" at the top left goes back to the rooms.

Stamps (small pictures)

- In a room, "Stamp" on the bottom line opens the list; click a picture and it
  is sent at once. Esc or "Stamp" again closes the list.
- The tabs on top: "My pictures" and the stamp packs installed from the App
  Store.
  - My pictures are the PNGs in /home/stamps/ (up to 48x48). Draw one in the
    Sprite Editor and save it there, and it is in the list the next time the
    app opens. A PNG drawn on a PC works too (see-through parts stay
    transparent).
  - Packs come from the App Store; the machine comes with no pictures of its
    own.
- The others see the picture even without the pack or the file: it travels
  through the chat host, once for each picture. A 16x16 picture is shown twice
  as large.
- A picture marked with a box is too large to send (48x48 and 8 KB at most);
  click it to see why.
- When the chat host no longer has a picture, "(could not get the stamp)" is
  shown in its place.

The window

- Drag the bottom-right corner to resize it. When wide, the rooms are on the
  left and the talk on the right; when narrow, you switch between them.

When something is wrong

The line at the bottom says why: the chat host is not found (ask your
family), this machine is not registered for the chat (tell your family the
machine's name shown there), the message is too long (200 characters, or
what the host allows), today's messages are used up, today's new pictures are
used up (pictures sent before still go), or the chat host has no room left for
pictures (tell your family). A message sent while the chat host is away goes
when it is back.

For the family: the chat host is asterism-chat (doc/asterism_chat). The
rooms, the machines it takes and the limits are its settings. The text of a
message stays only in its room's history, not in the host's records.
