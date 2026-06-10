# Risks and hardware constraints

## RP2350 RISC-V atomics in PSRAM

RP2350 does not provide normal coherent atomic/exclusive access to external PSRAM.
The existing kernel patch stack in this tree emulates or rewrites RISC-V atomic
operations so the kernel can run with code/data in PSRAM. Preserve those patches
until there is a better audited solution.

Practical rules:

* Do not remove the existing RP2350 atomic workaround patches.
* Avoid introducing new spinlock-like words or atomic-critical structures in PSRAM
  unless they are known to be covered by the workaround.
* Treat userspace runtimes that emit `lr/sc` or AMO instructions as risky. This
  target is BusyBox-only for the first milestones.
* Keep SMP disabled. The first Fruit Jam config uses one Hazard3 hart.
* If new early boot code needs lock words before the kernel workaround is active,
  place them in internal SRAM or avoid atomics entirely.

## Hardware assumptions requiring board validation

* PSRAM CS is configured as GPIO47. This is based on Adafruit's board definition and
  should be checked against the Fruit Jam Eagle schematic before claiming hardware
  validation.
* UART is on GPIO8/GPIO9 using UART1. The USB-C serial device is not the Linux
  console; use a 3.3 V hardware UART adapter.
* `fruitjamctl` touches low-risk GPIOs through `/dev/mem` for diagnostics. It is
  not a substitute for proper kernel GPIO/LED/input/USB-host/I2S/HSTX drivers.
* `fruitjamctl bootsel` uses the RP2350 bootrom watchdog-vector protocol and has
  been verified to re-enumerate as ROM BOOTSEL with `picotool info -a`.
* microSD, HSTX DVI, I2S audio, AirLift networking, and USB keyboard support are
  not implemented yet. Their pin mappings are documented to guide later work only.
* The NeoPixel driver uses PIO directly and is intentionally narrow: five onboard
  GPIO32 pixels through `/dev/neopixels`, not a general RP2350 PIO framework.

## Memory and image size

The board has only 8 MiB external PSRAM and no MMU. Keep the kernel, rootfs, log
buffer, and any future display buffers small. Do not enable large subsystems without
recording the image-size and memory-cost delta.
