# RP2350 no-MMU Linux Buildroot ports

This tree builds tiny Buildroot-based Linux images for RP2350 RISC-V (Hazard3)
boards. The original target is the Raspberry Pi Pico 2 / SparkFun Pro Micro RP2350
work; this branch adds an Adafruit Fruit Jam RP2350B bring-up target.

## Adafruit Fruit Jam RP2350B milestone

The Fruit Jam target is named `adafruit_fruit_jam_rp2350`. It uses:

* RV32 RISC-V Hazard3, no MMU.
* SMP disabled.
* BusyBox-only userspace.
* CramFS root filesystem in flash.
* Kernel copied to 8 MiB external PSRAM.
* UART1 console on the Fruit Jam `TX`/`RX` pins (GPIO8/GPIO9), 115200 8N1.
* USB CDC ACM console on `/dev/ttyGS0` when the gadget enumerates.
* Berry installed as `/usr/bin/berry`, including `-e`, script execution, and REPL.
* BusyBox `vi`.
* PIO-backed `/dev/neopixels` for the five onboard NeoPixels, plus
  `/root/neopixels.be` as a small Berry smoke-test program.
* `fruitjamctl` GPIO diagnostics for red LED, buttons, USB-host power enable,
  and shared TLV320/ESP32-C6 reset while real kernel drivers are still pending.

Validated on hardware:

* USB CDC shell on `/dev/cu.usbmodem1101`.
* Hardware UART shell/log path on `/dev/cu.usbserial-P97cvdxp`.
* `berry -e 'print("berry ok")'`.
* Berry REPL expression evaluation.
* `berry /root/neopixels.be`, visually confirmed on the onboard NeoPixels.
* `/bin/vi -> busybox`.

Known open item: `fruitjamctl bootsel` is still experimental. It currently drops
the USB CDC console, but has not yet reliably re-enumerated as the RP2350 ROM
BOOTSEL device on the test board.

Build and flash:

```bash
git submodule update --init
make -C buildroot BR2_EXTERNAL=$PWD adafruit_fruit_jam_rp2350_defconfig
make -C buildroot
picotool load -fu buildroot/output/images/flash-image.uf2
```

If `picotool` was not available during the Buildroot post-image step, generate the
UF2 manually:

```bash
picotool uf2 convert buildroot/output/images/flash-image.bin \
  buildroot/output/images/flash-image.uf2 --family rp2350-riscv
```

See [`board/adafruit/adafruit_fruit_jam_rp2350/README.md`](board/adafruit/adafruit_fruit_jam_rp2350/README.md)
for board assumptions, serial wiring, expected boot log, and limitations. See
[`docs/pinmap-fruitjam.md`](docs/pinmap-fruitjam.md), [`docs/risks.md`](docs/risks.md),
and [`docs/milestones.md`](docs/milestones.md) for follow-on work.

## Raspberry Pi Pico 2 / SparkFun Pro Micro RP2350

The original target is still available:

```bash
git submodule update --init
make -C buildroot BR2_EXTERNAL=$PWD raspberrypi-pico2_defconfig
make -C buildroot
picotool load -fu buildroot/output/images/flash-image.uf2
```

## Notes on RP2350 atomics

RP2350 reports a Store/AMO access fault if RISC-V AMO/exclusive operations target a
region that does not support atomics. External PSRAM is not normal cache-coherent RAM,
but this port places the kernel in PSRAM. The existing patch stack includes RP2350
workarounds for Linux atomics; do not remove those patches without a replacement and
a hardware test plan.
