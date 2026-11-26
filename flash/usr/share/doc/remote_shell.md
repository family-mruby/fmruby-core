# The remote shell

With the remote shell on, someone you allow can come into this machine from
afar, from an Asterism console (the Terminal in a browser), without being on
your home network. It is for help: they look at the files and apps here and
put things right.

It is off as shipped. Nobody comes in until you turn it on.

## 1. Turning it on

Open the Receiving app and press "Remote shell: off"; it becomes
"Remote shell: on". Press it again to turn it off. The choice is written in
/home/asterism/receive.toml and stays after the machine restarts.

Only someone who has both of these comes in:

- a place in the list of the "Shell" tab. Usually it is the one entry
  "from the console": the console sends it when its admin marks this
  machine.
- the console's permission: its admin lets that person use the remote
  shell (read only, or read and change).

And only over a checked connection: a tls/ router with certificates, or a
browser build opened from the console.

## 2. The mark while someone is in

While someone is in, the menu bar at the top shows "RS" (left of the kana
mode's cell). It goes when they leave (`exit`), or after 5 minutes without
a line.

## 3. What they can see and change

- See: the folders, the files' contents, the free space, the memory, the
  running apps, the machine's version.
- Change (only those allowed to): copy, rename and delete files, make
  folders, take files out to the console and put files from it, start and
  stop apps.
- Not: run a Ruby program of their own, use a full-screen program such as
  the editor, or work this machine's screen or keyboard.

Outside /home (the system's area: /usr, /etc and so on) can be read and
written too. Deleting a file of the system may keep the machine from
starting, so the console asks them once before they change anything there.

## 4. The keys' places are left out

The remote shell cannot read, write or delete these, nor list what is in them:

- /home/asterism (Asterism's keys and certificates, the receiving settings)
- /home/wifi.toml and /etc/wifi.toml (the Wi-Fi passwords)
- /home/secrets.toml and /etc/secrets.toml (the API keys)

The folders that hold them (/, /home, /etc, /home/asterism) cannot be
deleted or moved as a whole either. As the receiving settings are in a key's
place, the remote shell is turned on and off on this machine only.

## 5. Cutting it

Press "Remote shell: on" in the Receiving app to turn it off. Whoever is in
is refused from the next line, and the "RS" mark goes. Without the network
nobody comes in either.

In the browser build, closing the page shuts them out. Its files are kept in
the browser and stay after a reload, but a reloaded page is not reachable
until it is opened from the console again.
