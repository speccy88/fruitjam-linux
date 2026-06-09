# Adafruit Fruit Jam RP2350B Buildroot port

This board target is the first UART-only milestone for booting the Adafruit Fruit Jam
(product 6200) on the RP2350B Hazard3 RISC-V core with no MMU. It is based on the
existing RP2350 no-MMU Linux/Buildroot work in this tree and keeps the image small:
RV32, single core, BusyBox, CramFS in flash, and an 8 MiB PSRAM kernel load address.

## Board assumptions for Milestone A

* CPU mode: RP2350B Hazard3 RISC-V (`rp2350-riscv` UF2 family), not Cortex-M33.
* External flash: 16 MiB XIP flash mapped at `0x10000000`.
* External PSRAM: 8 MiB mapped at `0x11000000`.
* PSRAM chip select: QMI/XIP CS1 on GPIO47. This matches Adafruit's CircuitPython
  Fruit Jam board definition (`CIRCUITPY_PSRAM_CHIP_SELECT (&pin_GPIO47)`) and must
  be rechecked against the Eagle schematic net before changing bootloader timing.
* UART console: Fruit Jam `TX`/`RX` pins are GPIO8/GPIO9 using RP2350 UART1.
  Linux uses `earlycon=pl011,mmio32,0x40078000 console=ttyAMA0` with only this UART
  enabled in the Fruit Jam DT, so it is enumerated as `/dev/ttyAMA0`.
* Boot diagnostic LED: red LED on GPIO29, active low. The bootloader drives it low
  during startup and Linux includes `fruitjamctl` for later LED/button/USB-power
  bring-up diagnostics.

## Build

From the repository root after initializing the Buildroot submodule:

```sh
git submodule update --init
make -C buildroot BR2_EXTERNAL=$PWD adafruit_fruit_jam_rp2350_defconfig
make -C buildroot
```

Expected image outputs:

* `buildroot/output/images/flash-image.bin`
* `buildroot/output/images/flash-image.uf2` when `picotool` is available during the
  post-image step. If `picotool` is not available, run the conversion manually:

```sh
picotool uf2 convert buildroot/output/images/flash-image.bin \
  buildroot/output/images/flash-image.uf2 --family rp2350-riscv
```

## Flash

Put the Fruit Jam into the RP2350 USB bootloader with the BOOT button, then flash:

```sh
picotool load -fu buildroot/output/images/flash-image.uf2
```

## Serial console

Connect a 3.3 V UART adapter to the labeled Fruit Jam header pins:

* Adapter RX to Fruit Jam TX / GPIO8.
* Adapter TX to Fruit Jam RX / GPIO9.
* Adapter GND to Fruit Jam GND.

Open the console at `115200 8N1`, no flow control. Example:

```sh
picocom -b 115200 /dev/ttyUSB0
```

## Expected boot log

A successful Milestone A boot should show the bootloader, PSRAM detection, the Linux
banner, and a BusyBox prompt similar to:

```text
RP2350 Bootloader starting...
PSRAM setup complete. PSRAM size 0x800000 (8388608)
Jumping to kernel at 0x11000000 and DT at 0x100.....
Linux version 6.15 ...
Run /init as init process
#
```

Exact addresses and kernel timestamps will vary. Capture the full log for the PR or
hardware test report.

## Fruit Jam board-control helper

The image includes `fruitjamctl`, a small `/dev/mem`-based diagnostic helper for
the peripherals that are safe to touch before real kernel drivers exist:

```sh
fruitjamctl status
fruitjamctl buttons
fruitjamctl led on
fruitjamctl led off
fruitjamctl usb-power on
fruitjamctl periph-reset pulse
```

BusyBox init runs `fruitjamctl init` during boot to configure the three buttons
with pull-ups, turn the red LED off after the bootloader handoff, deassert the
shared TLV320/ESP32-C6 reset line, and enable USB host 5 V power. This does not
make USB HID, I2S audio, HSTX DVI, or NeoPixels complete Linux drivers; it is a
bring-up bridge so hardware validation can proceed over UART.

## Known limitations

* UART console only. HSTX DVI output is not implemented.
* USB host 5 V power can be enabled by `fruitjamctl`, but USB host and USB
  keyboard input protocol support are not implemented.
* microSD block support is not enabled or tested.
* Buttons are readable through `fruitjamctl`; WiFi/AirLift, I2S audio, DVI, and
  NeoPixels are not complete Linux drivers in this PR.
* RP2350 atomics are only safe in internal SRAM; see `docs/risks.md` before moving
  lock-heavy structures or userspace runtimes into PSRAM.
