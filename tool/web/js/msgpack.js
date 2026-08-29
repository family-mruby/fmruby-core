// Minimal MessagePack for the remote debugger protocol.
//
// Only what the debugd conversation needs, which is what tool/debug/
// fmrb_dbg_client.py puts on the wire: requests are
// [int, int, str, map|nil] and responses are [int, int, int, any]. Encoding
// therefore covers nil / bool / int / str / array / map, and decoding adds the
// wider integer and string widths plus bin, since the device picks the
// smallest form for each value.
//
// Strings go out as str (never bin), matching the client's use_bin_type=True.
// Encoded output is byte-identical to msgpack.packb() for these shapes; see
// the round-trip note in doc/reference/ble_c6_web_console.md.
(function (global) {
  'use strict';

  function Writer() {
    this.bytes = [];
  }
  Writer.prototype.u8 = function (v) { this.bytes.push(v & 0xFF); };
  Writer.prototype.raw = function (arr) {
    for (let i = 0; i < arr.length; i++) this.bytes.push(arr[i] & 0xFF);
  };
  Writer.prototype.be = function (v, n) {
    for (let i = n - 1; i >= 0; i--) this.bytes.push((v >>> (i * 8)) & 0xFF);
  };

  function encInt(w, v) {
    if (!Number.isInteger(v)) throw new Error('msgpack: non-integer number ' + v);
    if (v >= 0) {
      if (v < 0x80) return w.u8(v);                      // positive fixint
      if (v < 0x100) { w.u8(0xcc); return w.be(v, 1); }  // uint8
      if (v < 0x10000) { w.u8(0xcd); return w.be(v, 2); }// uint16
      w.u8(0xce); return w.be(v, 4);                     // uint32
    }
    if (v >= -32) return w.u8(0xE0 | (v + 32));          // negative fixint
    if (v >= -128) { w.u8(0xd0); return w.u8(v); }       // int8
    if (v >= -32768) { w.u8(0xd1); return w.be(v, 2); }  // int16
    w.u8(0xd2); w.be(v, 4);                              // int32
  }

  function encStr(w, s) {
    const b = new TextEncoder().encode(s);
    if (b.length < 32) w.u8(0xA0 | b.length);            // fixstr
    else if (b.length < 0x100) { w.u8(0xd9); w.be(b.length, 1); }
    else if (b.length < 0x10000) { w.u8(0xda); w.be(b.length, 2); }
    else { w.u8(0xdb); w.be(b.length, 4); }
    w.raw(b);
  }

  function encValue(w, v) {
    if (v === null || v === undefined) return w.u8(0xc0);
    if (typeof v === 'boolean') return w.u8(v ? 0xc3 : 0xc2);
    if (typeof v === 'number') return encInt(w, v);
    if (typeof v === 'string') return encStr(w, v);
    if (Array.isArray(v)) {
      if (v.length < 16) w.u8(0x90 | v.length);
      else if (v.length < 0x10000) { w.u8(0xdc); w.be(v.length, 2); }
      else { w.u8(0xdd); w.be(v.length, 4); }
      for (const item of v) encValue(w, item);
      return;
    }
    if (v instanceof Uint8Array) {
      if (v.length < 0x100) { w.u8(0xc4); w.be(v.length, 1); }
      else if (v.length < 0x10000) { w.u8(0xc5); w.be(v.length, 2); }
      else { w.u8(0xc6); w.be(v.length, 4); }
      return w.raw(v);
    }
    if (typeof v === 'object') {
      const keys = Object.keys(v);
      if (keys.length < 16) w.u8(0x80 | keys.length);
      else if (keys.length < 0x10000) { w.u8(0xde); w.be(keys.length, 2); }
      else { w.u8(0xdf); w.be(keys.length, 4); }
      for (const k of keys) { encStr(w, k); encValue(w, v[k]); }
      return;
    }
    throw new Error('msgpack: cannot encode ' + typeof v);
  }

  function encode(value) {
    const w = new Writer();
    encValue(w, value);
    return new Uint8Array(w.bytes);
  }

  function Reader(buf) {
    this.b = buf;
    this.p = 0;
  }
  Reader.prototype.need = function (n) {
    if (this.p + n > this.b.length) throw new Error('msgpack: truncated');
  };
  Reader.prototype.u8 = function () { this.need(1); return this.b[this.p++]; };
  Reader.prototype.be = function (n) {
    this.need(n);
    let v = 0;
    for (let i = 0; i < n; i++) v = v * 256 + this.b[this.p++];
    return v;
  };
  Reader.prototype.sbe = function (n) {
    const v = this.be(n);
    const lim = Math.pow(2, n * 8 - 1);
    return v >= lim ? v - lim * 2 : v;
  };
  Reader.prototype.str = function (n) {
    this.need(n);
    const s = new TextDecoder().decode(this.b.subarray(this.p, this.p + n));
    this.p += n;
    return s;
  };
  Reader.prototype.bin = function (n) {
    this.need(n);
    const s = this.b.slice(this.p, this.p + n);
    this.p += n;
    return s;
  };

  function decValue(r) {
    const t = r.u8();
    if (t < 0x80) return t;                              // positive fixint
    if (t >= 0xE0) return t - 256;                       // negative fixint
    if ((t & 0xF0) === 0x80) return decMap(r, t & 0x0F); // fixmap
    if ((t & 0xF0) === 0x90) return decArr(r, t & 0x0F); // fixarray
    if ((t & 0xE0) === 0xA0) return r.str(t & 0x1F);     // fixstr

    switch (t) {
      case 0xc0: return null;
      case 0xc2: return false;
      case 0xc3: return true;
      case 0xc4: return r.bin(r.be(1));
      case 0xc5: return r.bin(r.be(2));
      case 0xc6: return r.bin(r.be(4));
      case 0xca: { const v = new DataView(r.b.buffer, r.b.byteOffset + r.p, 4).getFloat32(0); r.p += 4; return v; }
      case 0xcb: { const v = new DataView(r.b.buffer, r.b.byteOffset + r.p, 8).getFloat64(0); r.p += 8; return v; }
      case 0xcc: return r.be(1);
      case 0xcd: return r.be(2);
      case 0xce: return r.be(4);
      case 0xcf: return r.be(8);
      case 0xd0: return r.sbe(1);
      case 0xd1: return r.sbe(2);
      case 0xd2: return r.sbe(4);
      case 0xd3: return r.sbe(8);
      case 0xd9: return r.str(r.be(1));
      case 0xda: return r.str(r.be(2));
      case 0xdb: return r.str(r.be(4));
      case 0xdc: return decArr(r, r.be(2));
      case 0xdd: return decArr(r, r.be(4));
      case 0xde: return decMap(r, r.be(2));
      case 0xdf: return decMap(r, r.be(4));
      default: throw new Error('msgpack: unsupported type 0x' + t.toString(16));
    }
  }

  function decArr(r, n) {
    const out = [];
    for (let i = 0; i < n; i++) out.push(decValue(r));
    return out;
  }

  function decMap(r, n) {
    const out = {};
    for (let i = 0; i < n; i++) {
      const k = decValue(r);
      out[k] = decValue(r);
    }
    return out;
  }

  function decode(buf) {
    return decValue(new Reader(buf instanceof Uint8Array ? buf : new Uint8Array(buf)));
  }

  global.msgpack = { encode, decode };
})(window);
