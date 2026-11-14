// Proof-of-concept wasm app (TinyGo). Same behaviour as the Rust and Zig apps.
package main

import "unsafe"

//go:wasmimport env fmrb_log
func fmrbLog(ptr unsafe.Pointer, length uint32)

//go:wasmimport env fmrb_plot
func fmrbPlot(x, y, color int32)

//go:wasmimport env fmrb_millis
func fmrbMillis() int64

//go:wasmimport env fmrb_nop
func fmrbNop(v int32) int32

var frame int32

//export on_create
func onCreate() {
	msg := "hello from tinygo"
	fmrbLog(unsafe.Pointer(unsafe.StringData(msg)), uint32(len(msg)))
	_ = fmrbMillis()
}

//export on_update
func onUpdate() int32 {
	f := frame
	for i := int32(0); i < 64; i++ {
		x := (i*7 + f*3) & 63
		y := (i*13 + f*5) & 63
		c := (i*31 + f*17) & 255
		fmrbPlot(x, y, c)
	}
	frame = f + 1
	return 16
}

// --- test and benchmark entry points (not part of the app API) ---

//export noop
func noop() {}

//export bench_compute
func benchCompute(n int32) int32 {
	var x uint32 = 1
	var acc uint32 = 0
	for i := int32(0); i < n; i++ {
		x = x*1103515245 + 12345
		acc += (x >> 16) ^ uint32(i)
	}
	return int32(acc)
}

//export bench_host_calls
func benchHostCalls(n int32) int32 {
	var acc int32
	for i := int32(0); i < n; i++ {
		acc += fmrbNop(i)
	}
	return acc
}

// call_indirect through a table of function values (feature-set check).
var ops = []func(int32) int32{
	func(k int32) int32 { return k + 1 },
	func(k int32) int32 { return k * 2 },
	func(k int32) int32 { return k ^ 0x55 },
}

//export dispatch
func dispatch(k int32) int32 {
	return ops[k%3](k)
}

var spinSink int32

//export spin
func spin() int32 {
	for {
		spinSink++
	}
}

var keep [][]byte

// grow allocates pages*64KiB on the Go heap and keeps it, so the heap has
// to grow the linear memory. Running out of memory traps.
//
//export grow
func grow(pages int32) int32 {
	b := make([]byte, int(pages)*65536)
	b[len(b)-1] = 1
	keep = append(keep, b)
	return int32(len(keep))
}

// alloc_churn allocates n buffers of size bytes and drops each one right
// away. With a freeing GC the linear memory stays flat; with the leaking GC
// it grows with n*size.
//
// churnSink makes each buffer escape to the heap; only the last one stays
// reachable.
var churnSink []byte

//export alloc_churn
func allocChurn(n, size int32) int32 {
	var sum int32
	for i := int32(0); i < n; i++ {
		b := make([]byte, size)
		b[0] = byte(i)
		churnSink = b
		sum += int32(b[0])
	}
	return sum
}

func main() {}
