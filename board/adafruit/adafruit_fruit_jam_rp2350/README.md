# Adafruit Fruit Jam RP2350B Buildroot port

This board target boots the Adafruit Fruit Jam (product 6200) on the RP2350B
Hazard3 RISC-V core with no MMU. It is based on the existing RP2350 no-MMU
Linux/Buildroot work in this tree and keeps the image small: RV32, single core,
BusyBox, CramFS in flash, and an 8 MiB PSRAM kernel load address.

For a community getting-started walkthrough, see
[Linux on Fruit Jam](https://adafruit-playground.com/u/mikeysklar/pages/linux-on-fruit-jam)
by Mikey Sklar on Adafruit Playground.

## Current hardware milestone

* CPU mode: RP2350B Hazard3 RISC-V (`rp2350-riscv` UF2 family), not Cortex-M33.
* External flash: 16 MiB XIP flash mapped at `0x10000000`.
* Flash rootfs partition: CramFS at 6 MiB, sized to 10 MiB through the end of flash.
* External PSRAM: 8 MiB mapped at `0x11000000`.
* PSRAM chip select: QMI/XIP CS1 on GPIO47. This matches Adafruit's CircuitPython
  Fruit Jam board definition (`CIRCUITPY_PSRAM_CHIP_SELECT (&pin_GPIO47)`) and must
  be rechecked against the Eagle schematic net before changing bootloader timing.
* UART console: Fruit Jam `TX`/`RX` pins are GPIO8/GPIO9 using RP2350 UART1.
  Linux uses `earlycon=pl011,mmio32,0x40078000 console=ttyAMA0` with only this UART
  enabled in the Fruit Jam DT, so it is enumerated as `/dev/ttyAMA0`.
* USB device console: the RP2350 USB device controller exposes a CDC ACM gadget
  console as `/dev/ttyGS0`.
* Boot diagnostic LED: red LED on GPIO29, active low. The bootloader drives it low
  during startup and Linux includes `fruitjamctl` for later LED/button/USB-power
  bring-up diagnostics.
* NeoPixels: GPIO32 is driven by the `rp2350_neopixel` PIO driver and exposed as
  `/dev/neopixels`.
* GPIO sysfs: `gpiochip0` exposes GPIO0..GPIO45 through `/sys/class/gpio`.
  GPIO47 is intentionally not exposed because it is the PSRAM chip-select line.
* microSD: SPI-mode card access uses PL022 SPI0 on GPIO34/GPIO35/GPIO36 with
  GPIO39 as a gpiolib chip select, mounted at `/mnt/sd`.
* Audio first step: `/dev/fruitjam-audio` drives PIO1 I2S on GPIO24/GPIO26/GPIO27
  plus MCLK on GPIO25, and `fruitjam-rtttl` plays RTTTL through that tone path.
* DVI first step: `/dev/fruitjam-dvi` exposes a tiny RGB332 HSTX frame path and
  `fruitjam-dvi` renders bounded text/dashboard/test frames.

Validated on the Fruit Jam board:

* Hardware UART shell/log path at 115200 8N1.
* USB CDC shell on `/dev/cu.usbmodem1101` from macOS.
* Berry installed at `/usr/bin/berry`.
* `berry -e 'print("berry ok")'`.
* Berry REPL expression evaluation.
* `/root/berry/fruitjam.be` provides an importable Berry hardware helper module
  for GPIO/buttons, ADC, I2C scan/ping, USB-host status, USB HID report decode,
  USB keyboard probe commands, AirLift diagnostics/TCP commands, MQTT command
  helpers, device presence, audio clock, DVI command writes, and NeoPixels.
* `/root/berry/neopixels.be` runs with `berry-run /root/berry/neopixels.be` and lights the
  five onboard NeoPixels.
* BusyBox `vi` is enabled as `/bin/vi`.
* `fruitjamctl bootsel` reboots into the RP2350 ROM BOOTSEL loader.
* `/proc/mtd` reports the CramFS partition as
  `mtd0: 00a00000 00001000 "cramfs"`.
* Sysfs GPIO exports work for GPIO29 output and GPIO0/GPIO4/GPIO5 button inputs:
  GPIO29 reads back output writes, while the released buttons read high.
* Default services start automatically: AirLift inbound HTTP/telnet/FTP through
  the ESP32-C6 when WiFi is configured, plus the `fruitjam-buttons` daemon.
  The loopback `fruitjam-services core` set starts `fruitjam-httpd`,
  `fruitjam-telnetd`, `fruitjam-ftpd`, and TFTP on demand for target-side smoke
  tests.
* USB CDC login shells use standalone `/usr/bin/hush`; the hardware UART uses
  `fruitjam-uart-login` to wait for Enter before execing `hush`, avoiding a
  no-MMU respawn loop when nothing is attached. Telnet sessions use the smaller
  `/usr/bin/fruitjam-shell` command loop with small history and tab completion
  to preserve no-MMU allocation headroom.
* `fruitjam-services status` reports service processes and TCP/UDP listeners
  without spawning `ps` or `netstat`, and CGI still runs after status/telnet
  checks.
* `fruitjam-mem` and `free` provide a tiny no-fork memory, uptime, load, and
  commit-pressure summary from `/proc` without enabling heavier BusyBox procps
  applets.
* `fruitjam-services init` creates `/mnt/sd/www/index.html` with a small
  "Fruit Jam stuff" placeholder only when the SD card does not already have an
  index page. HTTP `/` serves that SD-card page; `/playground` serves the
  built-in hardware playground from flash.
* `wget -O - http://127.0.0.1/index.html` serves the SD-card page after
  `fruitjam-services core`, and `wget -O - http://127.0.0.1/cgi-bin/env.cgi`
  returns `Fruit Jam CGI OK`.
* AirLift telnet on TCP/23 spawns `fruitjam-shell` and successfully echoed
  `TELNET_OK`; loopback telnet works after `fruitjam-services core`.
* Default AirLift startup is supervised by `fruitjam-services airlift-monitor`.
  If the inbound server exits after opening ESP32-C6 TCP listeners, the monitor
  reruns setup and starts it again; `/tmp/airlift-start.log` records the loop.
* microSD enumerates as `/dev/mmcblk0` and `/dev/mmcblk0p1`; `/mnt/sd` mounts
  as VFAT, accepts writes, unmounts, remounts, and preserves the test file.
* `airliftctl` talks to the onboard ESP32-C6 AirLift over RP2350 PL022/spidev
  hardware SPI. The installed NINA firmware reported `3.3.0`; WiFi join,
  DHCP address reporting, outbound HTTP via `airliftctl tcp-get example.com /`,
  inbound HTTP, inbound telnet, and passive FTP were verified on hardware.
  `airliftctl` serializes SPI access with a lock so direct diagnostics fail
  safely while the long-running inbound service owns the coprocessor; read-only
  `fw`, `mac`, `status`, and `ip` commands fall back to the last values in
  `/tmp/airlift-start.log`.
* `airliftctl mqtt-pub`/`mqtt-sub` and `mosquitto_pub`/`mosquitto_sub --airlift`
  provide userspace AirLift MQTT QoS 0 publish/subscribe paths with optional
  username/password authentication. The publish path is used for button actions
  when `MQTT_TRANSPORT=airlift` is set in the button config. A local broker
  received both a direct `fruitjam/test` AirLift publish and a synthetic
  `fruitjam/buttons/button2` publish from the button FIFO path.
* `/sys/class/misc/fruitjam-audio` registers, `/dev/fruitjam-audio` accepts
  `start` and `stop`, and `fruitjam-i2c ping 0x18` acks. Audible RTTTL output
  and a generated WAV scale were verified with the Mac microphone helper.
* `/dev/fruitjam-dvi` exists and `fruitjam-dvi dashboard` plus
  `fruitjam-dvi exec fruitjam-services status` returned successfully on the
  flashed image.

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

## Serial consoles

Connect a 3.3 V UART adapter to the labeled Fruit Jam header pins:

* Adapter RX to Fruit Jam TX / GPIO8.
* Adapter TX to Fruit Jam RX / GPIO9.
* Adapter GND to Fruit Jam GND.

Open the console at `115200 8N1`, no flow control. Example:

```sh
picocom -b 115200 /dev/ttyUSB0
```

When the USB gadget is up, a second shell is available over CDC ACM. On the tested
macOS host it enumerated as:

```sh
screen /dev/cu.usbmodem1101 115200
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

The image includes `fruitjamctl`, a small sysfs-GPIO diagnostic helper for the
peripherals that are safe to touch before fuller kernel drivers exist:

```sh
fruitjamctl status
fruitjamctl buttons
fruitjamctl led on
fruitjamctl led off
fruitjamctl usb-power on
fruitjamctl periph-reset pulse
fruitjamctl bootsel
```

BusyBox init runs `fruitjamctl init` during boot to configure the three buttons
with pull-ups, turn the red LED off after the bootloader handoff, deassert the
shared TLV320/ESP32-C6 reset line, and enable USB host 5 V power. This does not
make USB HID, I2S audio, or HSTX DVI complete Linux drivers; it is a bring-up
bridge so hardware validation can proceed over UART and USB CDC.
`/dev/fruitjam-usbhost` owns the USB host power switch plus GPIO1/GPIO2
line-state and reset timing in the kernel. `fruitjam-usbhost status`, `json`,
`wait`, `monitor`, `reset`, `decode`, `hid`, `kbd-find`, `kbd-text`,
`kbd-events`, and `kbd-shell`/`kbd-auto-shell` use that bridge when present and
fall back to sysfs GPIO on older images. The bridge stages the 32-word PIO2 full-speed host
program so USB packet work has a dedicated block that does not collide with PIO0
NeoPixels or PIO1 audio. PIO token/data transactions and boot-keyboard report
polling now have a narrow live text/events/shell path for boot-protocol
keyboards. The default target is address 1, configuration 1, interface 0, and
endpoint 1; `fruitjam-usbhost` can pass explicit address/config/interface/endpoint
values, or run the bounded `kbd-find`/`kbd-auto-*` scan, for keyboards that do
not match that simplest layout. Use
`./scripts/cdc-smoke-test.py --usb-keyboard --usb-keyboard-require-input` from
the repository root to prove the live keyboard text/events and helper shell path
on flashed hardware.
`fruitjam-hidkeys` decodes boot-protocol keyboard reports into text/events. It
also accepts DATA0/DATA1 `last_rx_hex` packets from `fruitjam-usbhost` when the
payload is an 8-byte keyboard report, so bridge captures can feed the same
tested key translation path.

The `bootsel` command requests a restart into the RP2350 ROM BOOTSEL loader. It
has been verified on Fruit Jam hardware by running `fruitjamctl bootsel` from the
Linux console and confirming `picotool info -a` reports `boot type: bootsel`.

## Sysfs GPIO

The image includes a small RP2350 SIO GPIO driver for bring-up. It is intentionally
simple and exists so legacy sysfs GPIO works before a full RP2350 pinctrl stack is
available:

```sh
echo 29 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio29/direction
echo 0 > /sys/class/gpio/gpio29/value
cat /sys/class/gpio/gpio29/value
echo 1 > /sys/class/gpio/gpio29/value
cat /sys/class/gpio/gpio29/value
```

GPIO29 is the active-low red LED, so raw value `0` turns it on and raw value `1`
turns it off. GPIO0/GPIO4/GPIO5 are configured with pull-ups when exported as
inputs, so released buttons read `1` and pressed buttons should read `0`.

## Button event logging

`fruitjam-services all` starts `fruitjam-buttons daemon`, which watches
GPIO0/GPIO4/GPIO5 and exposes a small test FIFO:

```sh
echo test button1 > /run/fruitjam-buttons.fifo
echo test button2 > /run/fruitjam-buttons.fifo
fruitjam-buttonlog dump /mnt/sd/fruitjam/buttons.db 10
cat /mnt/sd/fruitjam/buttons.log
```

Events are stored in `/mnt/sd/fruitjam/buttons.db` using a tiny fixed-schema
SQLite 3 file writer, and mirrored as text in `/mnt/sd/fruitjam/buttons.log` for
easy inspection on the target. The full SQLite CLI/library is intentionally not
included: the no-MMU image could not reliably allocate the large SQLite-linked
binary while BusyBox services were running.

To publish button events through AirLift after joining WiFi, create or edit
`/mnt/sd/fruitjam/buttons.conf`:

```sh
MQTT_HOST=broker.local
MQTT_PORT=1883
MQTT_TOPIC=fruitjam/buttons
MQTT_CLIENT_ID=fruitjam-rp2350
MQTT_USER=
MQTT_PASSWORD=
MQTT_TRANSPORT=airlift
```

The daemon calls `mosquitto_pub --airlift`, which execs `airliftctl mqtt-pub`
and sends one MQTT 3.1.1 QoS 0 publish over the ESP32-C6 NINA TCP socket API.
For finite subscribe tests, use `mosquitto_sub --airlift -h "$MQTT_HOST" -p
"$MQTT_PORT" -t 'fruitjam/#' -C 1 -W 30 -v`; add `-u`/`-P` when your broker
requires authentication. Berry programs can use the importable `fruitjam` module
to build or run the same AirLift-aware commands with `mqtt_pub_command`,
`mqtt_sub_command`, `mqtt_publish`, and `mqtt_subscribe`; the included
`09-mqtt-publish.be` and `10-mqtt-subscribe.be` examples generate
environment-configurable wrapper scripts under `/tmp`.

## I2C

The image exposes GPIO20/GPIO21 as a Linux GPIO-backed I2C master:

```sh
ls -l /dev/i2c-0
cat /sys/class/i2c-dev/i2c-0/name
fruitjam-i2c ping 0x18
fruitjam-i2c scan
```

On the tested Fruit Jam board, `/sys/class/i2c-dev/i2c-0/name` reported
`i2c-gpio0`, and `fruitjam-i2c scan` found address `0x18`, matching the
TLV320DAC3100 control interface.

## Audio

The first audio milestone is a tiny clock/control path, not a full ALSA driver:

```sh
cat /sys/class/misc/fruitjam-audio/dev
ls -l /dev/fruitjam-audio
echo start > /dev/fruitjam-audio
echo stop > /dev/fruitjam-audio
fruitjam-rtttl --tone 880 1200
FRUITJAM_AUDIO_RESET=pulse fruitjam-rtttl --tone 880 1200
fruitjam-rtttl
fruitjam-rtttl 'ok:d=8,o=5,b=120:c,e,g,c6'
```

`/dev/fruitjam-audio` starts PIO1 state machines that generate MCLK on GPIO25
and a simple 8 kHz, 16-bit stereo I2S stream on GPIO24/GPIO26/GPIO27.
`fruitjam-rtttl` configures the codec over `/dev/i2c-0`, then asks the kernel
helper to play each note as a sine tone. Full streamed PCM/I2S playback is still
future work. The `FRUITJAM_AUDIO_RESET=pulse` form is for serial/lab tests only;
the TLV320 reset line is shared with the AirLift ESP32-C6.

Host-side microphone verifier:

```sh
scripts/fruitjam-audio-mic-test.py --serial-port /dev/cu.usbserial-P97cvdxp --rtttl scale
scripts/fruitjam-audio-mic-test.py --serial-port /dev/cu.usbserial-P97cvdxp --install-sd-wav --run-wavplay --rtttl scale
```

Host-side example and CGI preflight:

```sh
scripts/validate-fruitjam-examples.sh
```

This compiles the web CGI for the host and checks JSON responses, verifies the
HTTP-callable Berry whitelist matches the installed `/root/berry` examples,
checks the SD-card Berry script reference path, parses all RTTTL tunes by
generating throwaway WAVs, and runs the finite Berry examples with graceful
hardware skips.

## AirLift networking

The onboard ESP32-C6 AirLift is controlled through the NINA SPI command protocol:

```sh
airliftctl fw
airliftctl status
airliftctl join <ssid> <passphrase>
airliftctl ip
airliftctl tcp-get example.com /
airliftctl mqtt-pub <broker-host> 1883 fruitjam/test hello
airliftctl mqtt-sub <broker-host> 1883 'fruitjam/#'
```

For automatic service startup, store WiFi credentials on the SD card at
`/mnt/sd/fruitjam/wifi.conf`:

```sh
mkdir -p /mnt/sd/fruitjam
chmod 700 /mnt/sd/fruitjam
cat > /mnt/sd/fruitjam/wifi.conf <<'EOF'
SSID=your-ssid
PASSWORD=your-passphrase
EOF
chmod 600 /mnt/sd/fruitjam/wifi.conf
```

Do not put real WiFi credentials in the rootfs overlay or release artifacts.

This is a userspace coprocessor socket helper. It proves that the AirLift can join
WiFi, open outbound TCP connections, and host a small inbound HTTP/telnet/FTP
surface, but it does not create a Linux `wlan0` interface. HTTP `/` serves user
pages from `/mnt/sd/www`, while `/playground` serves the built-in hardware
playground. BusyBox `wget` and normal Linux sockets still use loopback until a
kernel netdev driver or a fuller userspace bridge exists. The Linux image does
not flash the ESP32-C6 firmware; the tested board already had the Adafruit Fruit
Jam NINA `3.3.0` firmware.

## microSD

The image includes the SPI/MMC and VFAT pieces needed for the onboard microSD
slot:

```sh
cat /proc/partitions
ls -l /dev/mmc*
cat /proc/mounts
echo fruitjam_sd_final > /mnt/sd/fruitjam-linux-final.txt
cat /mnt/sd/fruitjam-linux-final.txt
umount /mnt/sd
mount /mnt/sd
cat /mnt/sd/fruitjam-linux-final.txt
```

The SD slot uses hardware SPI for SCK/MOSI/MISO and a GPIO chip select for CS.
Do not switch GPIO39 back to native PL022 chip select without a hardware test:
that path returned zero OCR from the card during bring-up.

## Berry and NeoPixels

Berry is installed as `/usr/bin/berry` with expression execution, script execution,
and REPL support:

```sh
berry -e 'print("hello fruit jam")'
berry
berry-run /root/berry/06-fruitjam-module.be
berry-run /root/berry/09-mqtt-publish.be
berry-run /root/berry/10-mqtt-subscribe.be
berry-run /root/berry/11-i2c.be
berry-run /root/berry/12-usbhost-keyboard.be
berry-run /root/berry/13-airlift.be
berry-run /root/berry/neopixels.be
```

The hardware playground can also run regular user scripts placed directly in
`/mnt/sd/berry`. They appear in the Berry selector as `SD: name.be` and are
executed by the same tiny JSON helper used for built-in examples:

```sh
mkdir -p /mnt/sd/berry
cp my-script.be /mnt/sd/berry/
```

The NeoPixel driver exposes `/dev/neopixels` with a simple text command interface:

```text
clear
fill R G B
set INDEX R G B
write
test
```

## Known limitations

* HSTX DVI has a tiny `/dev/fruitjam-dvi` RGB332 frame helper for bounded
  dashboard/text/test frames. Full fbdev/console support is not implemented.
* USB host 5 V power, D+/D- reset/line-state ownership, PIO packet I/O,
  parameterized boot-keyboard init/poll, bounded keyboard target auto-scan,
  text/events polling, and a tiny USB-keyboard-driven shell now live behind the
  `/dev/fruitjam-usbhost` kernel bridge. Hub, mass-storage, and general Linux
  input support are not implemented; composite keyboards may work only when the
  boot-keyboard interface and interrupt endpoint are within the small scan
  window.
* Buttons, GPIO29, microSD block access, button SQLite logging, GPIO20/GPIO21
  I2C, AirLift userspace socket access, first-step TLV320 RTTTL audio, and the
  narrow USB boot-keyboard probe work, but WiFi/AirLift Linux netdev support,
  full PCM/I2S audio, full DVI console, and general USB host input still need
  real Linux support.
* RP2350 atomics are only safe in internal SRAM; see `docs/risks.md` before moving
  lock-heavy structures or userspace runtimes into PSRAM.
