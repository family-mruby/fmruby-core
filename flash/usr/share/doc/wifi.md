# Wi-Fi settings

An ESP32 machine (Tab5, NARYA and the others) joins, by itself, the
strongest of the networks it remembers among those in reach. It looks
when it starts and whenever the connection drops.

## 1. Connecting from the screen

Open Network in the desktop's menu and press "Wi-Fi..." to open the Wi-Fi
window.

- The networks in reach are listed strongest first (the one in use at the
  top, with `>` before its name). The bars are the signal, the lock a
  secured network, "saved" a network it remembers.
- Choose one and press Connect. For a secured network it does not
  remember, a password field appears: type the password on the keyboard,
  then Connect (or Enter). It is shown as stars.
- Once connected, it remembers the network (in /home/wifi.toml). When the
  connection fails it remembers nothing and goes back to the networks it
  knows.
- Disconnect disconnects. It then stays off until you choose a network
  again or restart the machine.
- Forget removes the selected remembered network. Remembered networks out
  of reach are listed at the bottom, their names dimmed.
- Scan looks again. Opening the window looks once too.

It remembers up to five networks. Remembering a sixth drops the one
remembered longest ago.

## 2. The order it tries

1. The remembered networks in reach, strongest first.
2. Then the hidden networks (those that do not announce their name), by
   name.
3. When none of them works, it waits and looks again. The wait starts at
   5 seconds, doubles after each round that fails and stops growing at
   2 minutes; a connection brings it back to 5 seconds.

An open network (no password) is a candidate too, when it is remembered.

## 3. Writing wifi.toml by hand

The settings are in /home/wifi.toml (without it, the /etc/wifi.toml a
development build puts in is read; when /home/wifi.toml is there, it is
the only one read). It holds passwords, so no remote service (HTTP, BLE)
hands it out. Change it on the machine (the editor, the shell) or with the
BLE web console's Settings while its window is open. The web console adds
the network it writes to the remembered ones, as the screen does: first,
or updated when its name is already there (the others are kept; with more
than five, the oldest goes).

```toml
[wifi]
enable = true
# hostname = "fmruby"        # left out: fmruby-XXXXXX (the end of the MAC)

[[networks]]                 # newest first, up to five
ssid = "home"
password = "the password"

[[networks]]
ssid = "cafe"
password = ""                # an open network

[[networks]]
ssid = "hidden-net"          # a hidden SSID is added here, by hand
password = "the password"
hidden = true                # tried by name (a scan does not show it)
```

- A hidden SSID cannot be added from the screen: write it into this file
  with `hidden = true` as above.
- The older form (one `ssid` and `password` inside `[wifi]`) is still
  read; it becomes the first network. Remembering or forgetting a network
  on the screen writes the file again in the form above (comments in it
  are not kept).
- With a `[wifi]` table, `enable = true` is needed (as before).
- Whether Wi-Fi is used at all is `wifi_auto_start` in
  /etc/system_conf.toml. A NARYA (S3) cannot use Bluetooth and Wi-Fi at
  the same time: set `ble_auto_start = false` to use Wi-Fi there.

## 4. Seeing the global IP

The Network window can show the address this machine has on the internet
(its global IP). A machine cannot know that by itself, so you provide a URL
that answers with the caller's address on one line, and write that URL in
/etc/system_conf.toml. Without it the line is not shown (nothing is asked
by default).

```toml
# above the first line that starts with [ ([theme] and the like)
global_ip_url = "https://router.example.org/fmrb/ip"
```

- It is asked once each time the Network window opens (close and open it
  to ask again). While asking it says "Asking..."; when the answer is not one
  line holding an IP address, or the URL cannot be reached, it says
  "Could not find out". Nothing is asked while not connected.
- The URL should answer a GET with `text/plain`, the caller's address
  (IPv4 or IPv6) on one line. The browser build also needs
  `Access-Control-Allow-Origin: *` in the answer (and, from a page served
  over https, an https URL).
- Where: on an ESP32 machine, /etc/system_conf.toml (kept while only the
  firmware is flashed again, lost when the storage is flashed). The native
  program: /etc/system_conf.toml as well (kept across updates). The browser
  build: change the `global_ip_url = ""` line in /etc/system_conf.toml
  inside the machine and it is kept across a reload (URLs up to 93
  characters).
- The answer is only shown on the screen; it is not logged.
