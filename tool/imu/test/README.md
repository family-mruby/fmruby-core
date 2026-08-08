# BMI270 driver checks

`ruby tool/imu/test/verify.rb` loads the driver from
`lib/add/picoruby-bmi270/mrblib/bmi270.rb` and drives it against a fake device
that answers like a BMI270 and records everything written to it. It exits
non-zero on the first difference, so it works as a regression test after
changes to the driver.

The driver is plain Ruby with no dependency beyond the I2C object, which is
what makes this possible: the same file that runs on the device runs here.

## What it pins down

The screen shows whether the sensor works at all. What it cannot show is the
part that goes wrong silently:

| Check | Why it matters |
|---|---|
| Reset, power-save off, INIT_CTRL 0, load, INIT_CTRL 1, PWR_CTRL | The sensor accepts the configuration image only inside this bracket, and only reports measurements after it |
| The image arrives byte for byte at the right load address | The address counts 16-bit words, so an off-by-one there corrupts the image without any error being reported |
| Chunks are even-sized | Same reason: an odd chunk shifts every following byte by half a word |
| Failure cases return false | A missing or truncated image, a sensor that never reports a good load, an empty bus |
| Sign and scale of the measurements | Raw counts are 16-bit two's complement; a missed sign flip only shows as a level that leans the wrong way |

## What it cannot check

Whether the sensor is at 0x68 or 0x69 on the real board, whether it is on the
internal bus at all, and the actual timing of the load. Those need the
hardware.
