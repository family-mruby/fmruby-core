"""Exercise BleTransport's framing logic with a fake BleakClient (no radio).

Covers the parts that would otherwise only run on the user's Windows machine:
chunked writes, notification reassembly across fragments, multiple frames in
one notification, and corrupt-frame rejection.

Requires `bleak` to be importable (fmrb_ble_transport imports it at module
level); no Bluetooth adapter is needed.

Usage: python3 test_ble_transport.py
"""
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])

from fmrb_ble_framing import encode_frame           # noqa: E402
from fmrb_ble_transport import BleTransport         # noqa: E402

fails = []


def check(cond, label):
    print(f"  {'OK  ' if cond else 'FAIL'} {label}")
    if not cond:
        fails.append(label)


class FakeClient:
    def __init__(self, mtu=23):
        self.mtu_size = mtu
        self.writes = []

    async def write_gatt_char(self, uuid, data, response=False):
        assert response is False, "debug RX must use write-without-response"
        self.writes.append(bytes(data))


def make_transport(mtu=23):
    t = BleTransport("Family-mruby-test")
    t._start_loop()
    t._client = FakeClient(mtu)
    t._connected = True
    received = []
    t.set_body_handler(received.append)
    return t, received


print("== chunked send ==")
for mtu, body in [(23, b"hello"), (23, b"\x00" * 300), (517, bytes(range(256)) * 4),
                  (23, b""), (247, bytes([0xFF]) * 4096)]:
    t, _ = make_transport(mtu)
    try:
        t.send_body(body)
        chunks = t._client.writes
        expected = encode_frame(body)
        check(b"".join(chunks) == expected,
              f"mtu={mtu} body={len(body)}: chunks reassemble to the frame")
        check(all(len(c) <= mtu - 3 for c in chunks),
              f"mtu={mtu} body={len(body)}: every chunk <= mtu-3 ({mtu - 3})")
        # The delimiter must only ever be the final byte of the last chunk.
        joined = b"".join(chunks)
        check(joined.count(b"\x00") == joined[-1:].count(b"\x00") or
              b"\x00" not in joined[:-1],
              f"mtu={mtu} body={len(body)}: delimiter only at the end")
    finally:
        t._stop_loop()

print("== notification reassembly ==")
t, received = make_transport()
try:
    bodies = [b"first", b"\x00\x01\x02", bytes([(i * 5) % 256 for i in range(2000)])]
    stream = b"".join(encode_frame(b) for b in bodies)
    # Deliver the byte stream in awkward 17-byte fragments.
    for off in range(0, len(stream), 17):
        t._on_notify(None, stream[off:off + 17])
    check(received == bodies, f"3 frames reassembled from 17-byte fragments ({len(received)} got)")
finally:
    t._stop_loop()

print("== one notification carrying several frames ==")
t, received = make_transport()
try:
    bodies = [b"a", b"b", b"c"]
    t._on_notify(None, b"".join(encode_frame(b) for b in bodies))
    check(received == bodies, "3 frames in a single notification")
finally:
    t._stop_loop()

print("== corrupt frame is dropped, stream stays in sync ==")
t, received = make_transport()
try:
    good = encode_frame(b"good")
    bad = bytearray(encode_frame(b"bad!"))
    bad[2] ^= 0xFF                      # break the CRC without adding a 0x00
    t._on_notify(None, bytes(bad) + good)
    check(received == [b"good"], f"corrupt frame dropped, next frame delivered ({received})")
finally:
    t._stop_loop()

print("== send refused when the link is down ==")
t, _ = make_transport()
try:
    t._connected = False
    try:
        t.send_body(b"x")
        check(False, "send_body raises when disconnected")
    except ConnectionError:
        check(True, "send_body raises ConnectionError when disconnected")
finally:
    t._stop_loop()

print()
print("RESULT: PASS" if not fails else f"RESULT: FAIL ({len(fails)})")
sys.exit(1 if fails else 0)
