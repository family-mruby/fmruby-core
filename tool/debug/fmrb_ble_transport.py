#!/usr/bin/env python3
"""BLE GATT transport for the PicoRuby remote debugger.

Same interface as TcpTransport in fmrb_dbg_client.py (connect / close /
send_body / set_body_handler), so FmrbDebugClient does not care which one it
holds. The device side is main/drivers/ble/ble_task.c (debug service) plus
main/drivers/debug/fmrb_debug_transport_ble.c.

bleak is asyncio-only, so the event loop lives on a daemon thread and the
synchronous methods bridge onto it with run_coroutine_threadsafe. Every bridged
call has a timeout: a stalled BLE stack must raise, never deadlock the caller.

Requires `bleak` (>= 0.21). This module is imported only when a BLE target is
selected, so TCP users do not need it installed.
"""
import asyncio
import threading

from bleak import BleakClient, BleakScanner

from fmrb_ble_framing import decode_frame, encode_frame

# GATT UUIDs. The device builds these with BLE_UUID128_INIT, which takes the
# bytes little-endian, so the string form is that array reversed. byte[0] (the
# last hex pair here) selects the characteristic.
DEBUG_SERVICE_UUID = "4652414d-4252-5942-4c45-000000000005"
DEBUG_RX_UUID = "4652414d-4252-5942-4c45-000000000006"   # host -> device
DEBUG_TX_UUID = "4652414d-4252-5942-4c45-000000000007"   # device -> host

DEVICE_NAME_PREFIX = "Family-mruby-"

SCAN_TIMEOUT = 8.0          # seconds spent looking for advertisements
CONNECT_TIMEOUT = 15.0      # seconds for the whole connect sequence
SEND_TIMEOUT = 10.0         # seconds for one frame to reach the device
DEFAULT_CHUNK = 20          # fallback when the MTU is unavailable (WinRT)


class BleTransport:
    """COBS+CRC32 framed msgpack bodies over the debug GATT service."""

    def __init__(self, spec=None, timeout=5.0):
        """spec: device name, MAC address, or None to scan for a single one."""
        self.spec = spec
        self.timeout = timeout
        self._loop = None
        self._thread = None
        self._client = None
        self._on_body = None
        self._rxbuf = bytearray()
        self._connected = False
        self._address = None
        self._name = None

    def __str__(self):
        who = self._name or self._address or self.spec or "scan"
        return f"ble {who}"

    def set_body_handler(self, cb):
        self._on_body = cb

    # --- event loop plumbing ---------------------------------------------
    def _start_loop(self):
        self._loop = asyncio.new_event_loop()
        ready = threading.Event()

        def runner():
            asyncio.set_event_loop(self._loop)
            self._loop.call_soon(ready.set)
            self._loop.run_forever()

        self._thread = threading.Thread(target=runner, name="fmrb-ble", daemon=True)
        self._thread.start()
        ready.wait(5.0)

    def _stop_loop(self):
        if self._loop and self._loop.is_running():
            self._loop.call_soon_threadsafe(self._loop.stop)
        if self._thread:
            self._thread.join(timeout=5.0)
        self._loop = None
        self._thread = None

    def _run(self, coro, timeout):
        """Run a coroutine on the BLE loop and wait for it synchronously."""
        if not self._loop:
            raise ConnectionError("BLE transport is not running")
        future = asyncio.run_coroutine_threadsafe(coro, self._loop)
        try:
            return future.result(timeout)
        except asyncio.TimeoutError as e:
            future.cancel()
            raise TimeoutError(f"BLE operation timed out after {timeout}s") from e
        except TimeoutError:
            future.cancel()
            raise

    # --- discovery --------------------------------------------------------
    @staticmethod
    def _looks_like_address(spec):
        # "AA:BB:CC:DD:EE:FF" (BlueZ) or a Windows/macOS UUID-ish handle.
        return (spec.count(":") == 5 and len(spec) == 17) or \
               (spec.count("-") == 4 and len(spec) == 36)

    async def _discover(self):
        devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT)
        if self.spec:
            for d in devices:
                if d.name == self.spec:
                    return d
            names = sorted(d.name for d in devices if d.name)
            raise ConnectionError(
                f"BLE device named {self.spec!r} not found. Visible devices: "
                f"{', '.join(names) if names else '(none)'}")

        matches = [d for d in devices
                   if d.name and d.name.startswith(DEVICE_NAME_PREFIX)]
        if not matches:
            raise ConnectionError(
                f"no {DEVICE_NAME_PREFIX}* device found. Check that the board is "
                f"powered, BLE is on, and no other host is connected to it.")
        if len(matches) > 1:
            names = ", ".join(sorted(d.name for d in matches))
            raise ConnectionError(
                f"several devices found ({names}); name one explicitly, "
                f"e.g. ble:{sorted(d.name for d in matches)[0]}")
        return matches[0]

    # --- connection -------------------------------------------------------
    def connect(self):
        self._start_loop()
        try:
            self._run(self._connect(), CONNECT_TIMEOUT + SCAN_TIMEOUT)
        except Exception:
            self._stop_loop()
            raise

    async def _connect(self):
        if self.spec and self._looks_like_address(self.spec):
            # Skip the scan: useful when scanning is unreliable (sec 10).
            self._address = self.spec
            self._name = None
        else:
            device = await self._discover()
            self._address = device.address
            self._name = device.name

        self._client = BleakClient(
            self._address,
            disconnected_callback=self._on_disconnect,
            timeout=CONNECT_TIMEOUT,
        )
        await self._client.connect()
        self._rxbuf.clear()
        await self._client.start_notify(DEBUG_TX_UUID, self._on_notify)
        self._connected = True

    def _on_disconnect(self, _client):
        self._connected = False

    def close(self):
        if self._client and self._loop:
            try:
                self._run(self._disconnect(), 5.0)
            except Exception:
                pass
        self._connected = False
        self._client = None
        self._stop_loop()

    async def _disconnect(self):
        try:
            await self._client.stop_notify(DEBUG_TX_UUID)
        except Exception:
            pass
        await self._client.disconnect()

    # --- receive ----------------------------------------------------------
    def _on_notify(self, _sender, data):
        """Notification callback; runs on the BLE loop thread.

        Notifications are MTU-sized fragments, so accumulate until the 0x00
        delimiter closes a frame. The client's dispatch is thread safe, so it
        is fine to call the handler from here.
        """
        self._rxbuf.extend(data)
        while True:
            idx = self._rxbuf.find(b"\x00")
            if idx < 0:
                break
            frame = bytes(self._rxbuf[:idx])
            del self._rxbuf[:idx + 1]
            if not frame:
                continue
            body = decode_frame(frame)
            if body is None:
                # Corrupt or truncated: drop it. The device does the same, and
                # the pending request will surface as a timeout.
                continue
            if self._on_body:
                self._on_body(body)

    # --- send -------------------------------------------------------------
    def send_body(self, body):
        if not self._connected or not self._client:
            raise ConnectionError("BLE link is down")
        self._run(self._send(encode_frame(body)), SEND_TIMEOUT)

    async def _send(self, frame):
        chunk = DEFAULT_CHUNK
        try:
            mtu = self._client.mtu_size
            if mtu and mtu > 3:
                chunk = mtu - 3
        except Exception:
            pass   # WinRT does not always expose the MTU; the fallback is fine.

        for off in range(0, len(frame), chunk):
            await self._client.write_gatt_char(
                DEBUG_RX_UUID, frame[off:off + chunk], response=False)
