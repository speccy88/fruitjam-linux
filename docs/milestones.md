# Fruit Jam Linux milestones

## Milestone A: UART boot (this PR)

Goal: boot a minimal RV32 no-MMU Linux kernel and BusyBox shell over hardware UART.

Included:

* `adafruit_fruit_jam_rp2350_defconfig` Buildroot target.
* Fruit Jam device tree using UART1 at `0x40078000` as the only Linux console UART.
* Bootloader board constants for UART1 GPIO8/GPIO9, PSRAM CS GPIO47, and the active-low GPIO29 LED.
* `fruitjamctl` userspace diagnostics for red LED, buttons, USB host power enable, and shared peripheral reset.
* Build, flash, serial-console, and limitation notes.

Acceptance test:

1. Build the documented defconfig.
2. Confirm `buildroot/output/images/flash-image.bin` exists.
3. Confirm or generate `buildroot/output/images/flash-image.uf2`.
4. Flash with `picotool load -fu`.
5. Connect hardware UART at 115200 8N1.
6. Capture a boot log that reaches a BusyBox `#` prompt.

## Milestone B: clean board support

* Continue separating Fruit Jam constants from SparkFun/Pico 2 defaults.
* Validate every pin in `docs/pinmap-fruitjam.md` against schematic and hardware.
* Replace `fruitjamctl` `/dev/mem` diagnostics with real GPIO/LED/input drivers once RP2350 GPIO/pinctrl support exists.
* Add more robust early boot diagnostics if useful while keeping UART stable.

## Milestone C: microSD support

* Start with SPI mode only.
* Keep rootfs in flash/initramfs first.
* Add the minimum SPI/MMC/filesystem options.
* Demonstrate read-only mount of a microSD partition and document memory/image-size delta.

## Milestone D: HSTX DVI text console

* Do not start with a full graphics stack.
* Prefer a tiny text path: simple framebuffer/fbcon, custom text console, or a second-core HSTX helper.
* UART remains available as fallback.
* Add a build-time option to enable/disable HSTX console.

## Milestone E: USB keyboard

* Start with boot-protocol HID keyboard only.
* First useful test: typed characters appear in the Linux shell.
* Investigate Linux-side minimal host/input code versus a second-core firmware bridge.
* Do not include mouse, storage, arbitrary hub hotplug, or composite-device support in the first keyboard milestone.
