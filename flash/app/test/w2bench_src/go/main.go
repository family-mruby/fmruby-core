// w2bench (TinyGo): the W2 speed probe, the same program as c/w2bench.c.
package main

import "github.com/family-mruby/family-mruby-sdk/tinygo/fmrb"

const (
	nInt  = 100000
	nFill = 200
	nText = 100
	nCall = 1000
)

type bench struct{ step int32 }

func report(test string, n int32, us, sum int64) {
	line := "W2B go " + test + " n=" + fmrb.Itoa(int64(n)) + " us=" + fmrb.Itoa(us) + " sum=" + fmrb.Itoa(sum)
	fmrb.LogInfo(line)
	fmrb.FileAppend("/tmp/w2b.txt", []byte(line+"\n"))
}

func (b *bench) OnCreate() { fmrb.LogInfo("W2B go start") }

func (b *bench) OnUpdate() int32 {
	if b.step >= 12 {
		fmrb.App.Stop()
		return 1000
	}
	kind := b.step % 4
	b.step++
	g := fmrb.Window
	t0 := fmrb.UptimeUs()
	switch kind {
	case 0:
		s := int32(1)
		for i := int32(0); i < nInt; i++ {
			s = ((s*31 + i) ^ (s >> 3)) & 0xFFFFF
		}
		report("int", nInt, fmrb.UptimeUs()-t0, int64(s))
	case 1:
		for i := int32(0); i < nFill; i++ {
			g.FillRect((i*8)%160, ((i/20)*8)%64, 8, 8, (i*7)&0xFE)
		}
		us := fmrb.UptimeUs() - t0
		g.Present()
		report("fill", nFill, us, 0)
	case 2:
		for i := int32(0); i < nText; i++ {
			g.DrawText((i%4)*40, ((i/4)%6)*10, "0123456789", 0xFF)
		}
		us := fmrb.UptimeUs() - t0
		g.Present()
		report("text", nText, us, 0)
	default:
		sum := int64(0)
		for i := 0; i < nCall; i++ {
			sum += int64(g.AreaWidth())
		}
		report("call", nCall, fmrb.UptimeUs()-t0, sum)
	}
	return 200
}

func init() { fmrb.SetApp(&bench{}) }
func main() {}
