#!/usr/bin/env python3
"""COBS + CRC32 frame codec for the BLE debug transport.

Mirrors the device implementation in main/drivers/ble/ble_framing.c and the
frame layout in doc/vm_remote_debug_protocol.md section 1 ("BLE GATT"):

    plain = [u16 BE body length][msgpack body][CRC32 4B BE]
    frame = COBS(plain) + b"\\x00"

The CRC32 covers the length header and the body, and is computed before COBS
encoding. binascii.crc32 uses the same polynomial as the device table
(reflected 0xEDB88320), so the two agree byte for byte.

Pure functions only: no bleak, no sockets. Run this module directly to execute
the self-test, which includes fixed vectors that must stay in sync with the
device.
"""
import binascii
import struct

# Limits from fmrb_debug_proto.h / ble_debug_link.h.
MAX_BODY = 4096       # FMRB_DEBUG_MAX_FRAME
MAX_PLAIN = MAX_BODY + 2 + 4
MAX_ENCODED = MAX_BODY + 2 + 4 + ((MAX_BODY + 6) // 254) + 2   # BLE_DBG_MAX_ENC
DELIMITER = b"\x00"


def cobs_encode(data: bytes) -> bytes:
    """COBS-encode data. The trailing delimiter is NOT appended."""
    out = bytearray(b"\x00")   # placeholder for the first code byte
    code_idx = 0
    code = 1
    for byte in data:
        if byte == 0:
            out[code_idx] = code
            code = 1
            code_idx = len(out)
            out.append(0)
        else:
            out.append(byte)
            code += 1
            if code == 0xFF:
                out[code_idx] = code
                code = 1
                code_idx = len(out)
                out.append(0)
    out[code_idx] = code
    return bytes(out)


def cobs_decode(data: bytes, max_out: int = MAX_PLAIN) -> bytes:
    """COBS-decode data, stopping at a 0x00 byte or at the end of the input.

    Mirrors the device decoder, including its output cap: a corrupt frame that
    would expand past max_out is truncated (and then rejected by the CRC).
    """
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        code = data[i]
        i += 1
        if code == 0:
            break
        for _ in range(1, code):
            if i >= n or len(out) >= max_out:
                break
            out.append(data[i])
            i += 1
        if code < 0xFF and len(out) < max_out and i < n:
            out.append(0)
    return bytes(out)


def encode_frame(body: bytes) -> bytes:
    """Wrap a msgpack body into a complete on-wire frame (delimiter included)."""
    if len(body) > MAX_BODY:
        raise ValueError(f"body too large: {len(body)} > {MAX_BODY}")
    plain = struct.pack(">H", len(body)) + body
    plain += struct.pack(">I", binascii.crc32(plain) & 0xFFFFFFFF)
    frame = cobs_encode(plain) + DELIMITER
    if len(frame) > MAX_ENCODED:
        raise ValueError(f"encoded frame too large: {len(frame)} > {MAX_ENCODED}")
    return frame


def decode_frame(frame: bytes):
    """Unwrap one on-wire frame. Returns the msgpack body, or None if invalid.

    The trailing delimiter may be present or already stripped.
    """
    if frame.endswith(DELIMITER):
        frame = frame[:-1]
    plain = cobs_decode(frame)
    if len(plain) < 6:
        return None
    got_crc = struct.unpack(">I", plain[-4:])[0]
    if got_crc != (binascii.crc32(plain[:-4]) & 0xFFFFFFFF):
        return None
    body_len = struct.unpack(">H", plain[:2])[0]
    if body_len != len(plain) - 6:
        return None
    return plain[2:-4]


# --- self-test ------------------------------------------------------------
def _self_test():
    failures = []

    def check(cond, label):
        print(f"  {'OK  ' if cond else 'FAIL'} {label}")
        if not cond:
            failures.append(label)

    print("== COBS fixed vectors ==")
    # These are the classic COBS vectors and must match ble_framing.c exactly.
    check(cobs_encode(b"") == b"\x01", "encode(b'') == 01")
    check(cobs_encode(b"\x00") == b"\x01\x01", "encode(00) == 01 01")
    check(cobs_encode(b"\x00\x00") == b"\x01\x01\x01", "encode(00 00) == 01 01 01")
    check(cobs_encode(b"\x11\x22\x00\x33") == b"\x03\x11\x22\x02\x33",
          "encode(11 22 00 33) == 03 11 22 02 33")
    check(cobs_encode(b"\x11\x22\x33\x44") == b"\x05\x11\x22\x33\x44",
          "encode(11 22 33 44) == 05 11 22 33 44")
    # 254 non-zero bytes: the device encoder closes the block and opens a new
    # (empty) one, so the output carries a trailing 0x01.
    v254 = bytes([0xAA]) * 254
    enc254 = cobs_encode(v254)
    check(enc254 == b"\xff" + v254 + b"\x01", "encode(254 x AA) == ff <254> 01")
    check(len(enc254) == 256, "encode(254 x AA) length == 256")
    check(cobs_decode(enc254) == v254, "decode(254 x AA) round-trips")

    print("== COBS round-trip ==")
    for label, data in [
        ("empty", b""),
        ("single zero", b"\x00"),
        ("all zeros x 300", b"\x00" * 300),
        ("no zeros x 300", bytes(range(1, 256)) * 2),
        ("mixed", bytes([i % 256 for i in range(1000)])),
        ("max plain", bytes([(i * 7) % 256 for i in range(MAX_PLAIN)])),
    ]:
        check(cobs_decode(cobs_encode(data)) == data, f"round-trip: {label}")

    print("== frame round-trip ==")
    for label, body in [
        ("empty body", b""),
        ("one byte", b"\x01"),
        ("body with zeros", b"\x00" * 100),
        ("max body", bytes([(i * 13) % 256 for i in range(MAX_BODY)])),
    ]:
        frame = encode_frame(body)
        check(decode_frame(frame) == body, f"frame round-trip: {label}")
        check(DELIMITER not in frame[:-1], f"no interior delimiter: {label}")
        check(len(frame) <= MAX_ENCODED, f"within MAX_ENCODED: {label}")

    # Worst case must fit the device buffer exactly (see progress log).
    worst = encode_frame(bytes([0xFF]) * MAX_BODY)
    check(len(worst) == MAX_ENCODED,
          f"worst-case frame is exactly MAX_ENCODED ({MAX_ENCODED})")

    print("== rejection ==")
    good = encode_frame(b"hello")
    check(decode_frame(b"\x01" + DELIMITER) is None, "too-short frame rejected")
    corrupt = bytearray(good)
    corrupt[2] ^= 0xFF
    check(decode_frame(bytes(corrupt)) is None, "CRC corruption rejected")
    check(decode_frame(good[:4]) is None, "truncated frame rejected")
    try:
        encode_frame(b"x" * (MAX_BODY + 1))
        check(False, "oversized body raises ValueError")
    except ValueError:
        check(True, "oversized body raises ValueError")

    print("== fixed frame vector (cross-check against the device) ==")
    # body = msgpack.packb([0, 1, "version", None]) -- the first request the
    # client sends. If a real device ever disagrees with this frame, compare
    # here before touching either implementation.
    # The expected frame below was produced by ble_framing.c itself, not by
    # this module (see doc/vm_remote_debug_progress.md, Phase 3c).
    version_body = bytes.fromhex("940001a776657273696f6ec0")
    version_frame = encode_frame(version_body)
    expected = "01030c940f01a776657273696f6ec0654980c900"
    check(version_frame.hex() == expected,
          f"version frame == {expected} (got {version_frame.hex()})")
    check(decode_frame(version_frame) == version_body, "version frame decodes back")

    print()
    print("RESULT: PASS" if not failures else f"RESULT: FAIL ({len(failures)})")
    return 1 if failures else 0


if __name__ == "__main__":
    import sys
    sys.exit(_self_test())
