# Serial (UART)

Apps can exchange text and bytes with a serial device wired to the board's
pins (a LoRa radio, a GPS, another microcontroller ...). To just try it, open
"Serial Terminal" from the menu, pick the unit, the pins and the speed, press
Open, then type a line in the box at the bottom and press Enter to send it.
What arrives is listed in the middle.

## Wiring

- **Cross TX and RX**: our TX goes to the other side's RX, our RX to its TX.
- **Always connect GND** (so both sides share the same reference).
- The signals are **3.3 V**. A device with 5 V signals (an older Arduino and
  the like) can damage the board when wired directly: put a level shifter in
  between.
- Use the same speed (baud rate) on both sides. 9600 and 115200 are common.

## Units and pins per board

A unit is the UART's number (1 for UART1). The pins in the table are
examples; other free pins can be chosen too.

| Board | Units an app may open | Example pins (TX, RX) |
|---|---|---|
| Tab5 | 1, 2, 3, 4 | the Grove port, G53 and G54 |
| NARYA v4 (P4-Nano) | 1, 2, 3, 4 | free pins of the expansion header |
| AtomS3R | 1, 2 | Grove, 2 and 1 |
| StickS3 | 1, 2 | Grove, 9 and 10 |
| NARYA v3 | 2 | GROVE 2, 47 and 48 |

- UART0 (the console) and NARYA v3's UART1 (the link to the drawing side) are
  the system's and cannot be opened.
- Pins used for the screen, the SD card, the radio, sound and the like cannot
  be chosen; you are told why.
- On the Linux sim only UART2 opens, and what you write comes straight back
  (for trying things without a device). The browser and the PC builds have no
  UART.

## Writing an app

```ruby
class Hello < FmrbApp
  def on_create
    # unit and pins are an example
    @u = UART.new(unit: 1, txd_pin: 53,
      rxd_pin: 54, baudrate: 115200)
    @u.puts "hello"
  end

  def on_update
    line = @u.gets       # never waits
    Log.info(line) if line
    50
  end
end
```

- Reading (`gets`, `read`, `getbyte`) never waits: it gives nil until
  something has arrived, so call it in every `on_update`.
- When the app ends, the system closes the UARTs it opened.
- When it cannot be opened you get an IOError (in use, a system pin ...) or an
  ArgumentError (no such unit or pin ...), with the reason in the message.
- For the details, put the cursor on `UART` in the editor and press F1.

## Not together with the serial MIDI output

Once a MIDI app sends to "serial", MIDI keeps UART2 and its TX pin (the first
line of the Grove port) until the machine restarts. After that, that unit and
pin cannot be opened as a UART ("used by the MIDI serial output"). Use the
UART before the serial MIDI output, or after a restart. The other way round,
while a UART uses the Grove pins the serial MIDI output does not open.

## When it does not work

- **Nothing arrives**: check that TX and RX are crossed, GND is connected and
  the speed is the same on both sides.
- **Garbled characters**: the speed or the format differs from the other side
  (8 data bits, no parity, 1 stop bit is the usual one).
- **"in use by another app"**: another app has the same unit open. Close it.
- **"used by the system"**: a unit or pin the system uses. Pick another one.
- **Dropped bytes** (Serial Terminal's "Dropped bytes", `dropped`): reading
  does not keep up. Read more often, or give a larger `rx_buffer_size:` (up to
  16384).
