# Fruit Jam Linux milestones

## Milestone A: UART boot

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

Status: achieved on Fruit Jam hardware.

## Milestone B: USB CDC, Berry, vi, and NeoPixels

Goal: make the board pleasant to use without relying only on the hardware UART.

Included:

* USB CDC ACM gadget console on `/dev/ttyGS0`.
* Berry package installed as `/usr/bin/berry`.
* Berry `-e`, script execution, and REPL.
* BusyBox `vi`.
* PIO-backed `/dev/neopixels` driver for the five onboard GPIO32 NeoPixels.
* `/root/berry/neopixels.be` Berry smoke-test script.

Acceptance test:

1. Boot the image and connect to USB CDC at 115200.
2. Confirm a shell prompt over CDC.
3. Run `berry -e 'print("berry ok")'`.
4. Enter the Berry REPL and evaluate `1+2`.
5. Confirm `/bin/vi` exists and points to BusyBox.
6. Run `berry-run /root/berry/neopixels.be` and visually confirm the five-pixel pattern.
7. Run `fruitjamctl bootsel` and confirm `picotool info -a` reports
   `boot type: bootsel`.

Status: achieved on Fruit Jam hardware.

## Milestone C: sysfs GPIO and loopback services

Goal: make the image usable as a tiny local network and hardware-control box over
UART or USB CDC while preserving enough memory for interactive shells.

Included:

* Flash layout reserves a 10 MiB CramFS partition from the 6 MiB flash offset to
  the end of the 16 MiB flash device.
* A minimal RP2350 SIO GPIO driver exposes GPIO0..GPIO45 as `gpiochip0` through
  legacy `/sys/class/gpio`.
* GPIO29 works as an active-low LED output and reads back output latch writes.
* GPIO0/GPIO4/GPIO5 export as pulled-up button inputs.
* BusyBox httpd and tftpd, plus tiny `nc`, `wget`, `fruitjam-ftpd`,
  `fruitjam-telnetd`, and `fruitjam-services`.
* Serial consoles use standalone `/usr/bin/hush`; telnet sessions use
  `/usr/bin/fruitjam-shell` to reduce no-MMU contiguous allocation pressure.
* `fruitjam-services status` reports service processes and TCP/UDP listeners by
  reading `/proc` directly instead of spawning larger `ps`/`netstat` applets.
* Kernel config trimmed to keep USB CDC gadget support while removing Linux USB
  host core and broad Ethernet/WLAN driver families from this image.

Acceptance test:

1. Boot the image and connect over hardware UART or USB CDC.
2. Confirm `/proc/mtd` reports `mtd0: 00a00000 00001000 "cramfs"`.
3. Export GPIO29, set it output, write `0` then `1`, and confirm reads return
   `0` then `1`.
4. Export GPIO0/GPIO4/GPIO5 as inputs and confirm released buttons read `1`.
5. Run `fruitjam-services status` and confirm listeners on TCP/21, TCP/23,
   TCP/80, and UDP/69.
6. Run `wget -O - http://127.0.0.1/index.html` and confirm the Fruit Jam Linux
   status page.
7. Run `telnet 127.0.0.1 23`, execute `echo TELNET_OK`, and exit.
8. Re-run `wget -O - http://127.0.0.1/cgi-bin/env.cgi` after service status and
   telnet checks to confirm CGI still has allocation headroom.

Status: achieved on Fruit Jam hardware.

## Milestone C2: button actions and SD-backed SQLite log

Goal: keep the three onboard buttons useful while the network services are
running, without depending on a large SQLite process on a no-MMU system.

Included:

* `fruitjam-buttons daemon` watches GPIO0/GPIO4/GPIO5.
* `/run/fruitjam-buttons.fifo` accepts synthetic test events such as
  `echo test button1 > /run/fruitjam-buttons.fifo`.
* `mosquitto_pub` compatible helper is installed for MQTT publish attempts when
  `MQTT_HOST` is configured. Set `MQTT_TRANSPORT=airlift` to route the same
  button publish command through `mosquitto_pub --airlift` and the ESP32-C6 NINA
  TCP socket API.
* `fruitjam-buttonlog` writes a fixed-schema SQLite 3 database at
  `/mnt/sd/fruitjam/buttons.db` and a text mirror at
  `/mnt/sd/fruitjam/buttons.log`.
* The full Buildroot SQLite package is intentionally disabled; the full SQLite
  CLI/amalgamation required too much contiguous memory for this no-MMU image.

Acceptance test:

1. Boot the image and connect over hardware UART or USB CDC.
2. Run `fruitjam-services status` and confirm `fruitjam-buttons daemon` is
   running while TCP/21, TCP/23, TCP/80, and UDP/69 listeners are present.
3. Confirm `/run/fruitjam-buttons.fifo` exists.
4. Run `echo test button3 > /run/fruitjam-buttons.fifo`, then
   `echo test button1 > /run/fruitjam-buttons.fifo`.
5. Confirm `/mnt/sd/fruitjam/buttons.db` exists and is 8192 bytes.
6. Run `fruitjam-buttonlog dump /mnt/sd/fruitjam/buttons.db 10` and confirm the
   synthetic events are listed.
7. Re-run `wget -O - http://127.0.0.1/index.html` and
   `wget -O - http://127.0.0.1/cgi-bin/env.cgi` after logging events.
8. With AirLift joined to WiFi and an MQTT broker reachable, configure
   `/mnt/sd/fruitjam/buttons.conf` with `MQTT_HOST`, `MQTT_TOPIC`, and
   `MQTT_TRANSPORT=airlift`, then trigger a synthetic button event and confirm
   the broker receives the QoS 0 message.

Status: achieved on Fruit Jam hardware. Synthetic FIFO events are logged to the
SD-backed SQLite file, and with `MQTT_TRANSPORT=airlift` a `button2` test event
was received by a local MQTT broker as topic `fruitjam/buttons/button2`.

## Milestone C follow-up: clean board support

* Continue separating Fruit Jam constants from SparkFun/Pico 2 defaults.
* Validate every pin in `docs/pinmap-fruitjam.md` against schematic and hardware.
* Keep `fruitjamctl` on the narrow sysfs GPIO path and add real LED/input drivers
  once RP2350 GPIO/pinctrl support grows beyond bring-up needs.
* Add more robust early boot diagnostics if useful while keeping UART stable.

## Milestone D: microSD support

Goal: expose the Fruit Jam microSD slot as a Linux block device while keeping the
rootfs in flash.

Included:

* RP2350 PL022 SPI0 SD-card path on GPIO34/GPIO35/GPIO36.
* SD card chip select on GPIO39 driven through `cs-gpios`, not native PL022 CS.
* `mmc_spi`, block partition, VFAT, and NLS support in the kernel config.
* `/dev/mmcblk0` and `/dev/mmcblk0p1` nodes from devtmpfs.
* `/mnt/sd` mount point and `/etc/fstab` entry for `/dev/mmcblk0p1`.

Acceptance test:

1. Boot the image and connect over hardware UART or USB CDC.
2. Confirm `/proc/partitions` reports `mmcblk0` and `mmcblk0p1`.
3. Confirm `/dev/mmcblk0` and `/dev/mmcblk0p1` exist.
4. Confirm `/proc/mounts` shows `/dev/mmcblk0p1 /mnt/sd vfat`.
5. Write `fruitjam_sd_final` to `/mnt/sd/fruitjam-linux-final.txt`.
6. Read the file back, unmount `/mnt/sd`, remount `/mnt/sd`, and read it again.

Status: achieved on Fruit Jam hardware.

## Milestone D2: GPIO-backed I2C on GPIO20/GPIO21

Goal: expose the Fruit Jam STEMMA/QT-style I2C pins through normal Linux
`i2c-dev` so later TLV320DAC3100 control and small I2C tools have a real bus.

Included:

* `i2c-gpio` master using GPIO20 as SDA and GPIO21 as SCL.
* Internal pull-ups enabled for GPIO20/GPIO21 in the RP2350 SIO GPIO driver.
* `/dev/i2c-0` from `i2c-dev`.
* `fruitjam-i2c scan` and `fruitjam-i2c ping <addr>` helper.

Acceptance test:

1. Boot the image and connect over hardware UART or USB CDC.
2. Confirm `/dev/i2c-0` exists.
3. Confirm `/sys/class/i2c-dev/i2c-0/name` reports `i2c-gpio0`.
4. Run `fruitjam-i2c ping 0x18` and confirm the TLV320DAC3100 responds.
5. Run `fruitjam-i2c scan` and confirm address `18` appears.
6. Re-run `fruitjam-services status`, `wget` for `/index.html` and
   `/cgi-bin/env.cgi`, button FIFO logging, and GPIO29 readback.

Status: achieved on Fruit Jam hardware; `fruitjam-i2c scan` found `0x18`.

## Milestone D3: first TLV320DAC3100 audio path

Goal: prove that Linux can configure the TLV320DAC3100 and produce speaker audio
without a full ALSA/PCM driver.

Included:

* `fruitjam-audio-clock` misc driver exposed as `/dev/fruitjam-audio`.
* PIO1 generates MCLK on GPIO25 and an 8 kHz stereo I2S stream on
  GPIO24/GPIO26/GPIO27, using 16-bit samples in 32-bit slots.
* `fruitjam-rtttl` starts the audio clock, configures the TLV320DAC3100 over
  `/dev/i2c-0`, and asks the kernel helper to play RTTTL notes as sine tones.
* The driver maps shared RP2350 register windows with `devm_ioremap()` instead
  of exclusive `devm_platform_ioremap_resource_byname()`, because IO_BANK0 and
  pad registers are shared with the GPIO, NeoPixel, and AirLift helpers.

Acceptance test:

1. Boot the image and connect over hardware UART or USB CDC.
2. Confirm `/sys/class/misc/fruitjam-audio/dev` exists.
3. Confirm `/dev/fruitjam-audio` is a character device.
4. Run `fruitjam-i2c ping 0x18` and confirm the TLV320DAC3100 responds.
5. Run `echo start > /dev/fruitjam-audio` and `echo stop > /dev/fruitjam-audio`;
   both should return exit status 0.
6. Run `/usr/bin/fruitjam-rtttl --tone 880 1200` as a single-tone calibration,
   then run `/usr/bin/fruitjam-rtttl scale` while recording with the Mac microphone:
   `scripts/fruitjam-audio-mic-test.py --serial-port <port> --rtttl scale`.
   Confirm the verifier reports matched note frequencies and `PASS`.
7. Generate and copy a sample WAV, then run `fruitjam-wavplay` while recording
   with the same microphone verifier. Keep SD-card filesystem failures separate
   from audio failures; a bad FAT read should not be counted as a codec
   regression.
8. Re-run `wget -O - http://127.0.0.1/cgi-bin/env.cgi` and
   `fruitjam-services status`.

Status: achieved for the first RTTTL audio path on Fruit Jam hardware. The
current verified path is TLV320 control plus PIO I2S generated tones, confirmed
with the Mac microphone helper. Full streamed PCM/I2S playback is still future
work.

## Milestone E: HSTX DVI text console

* Do not start with a full graphics stack.
* Prefer a tiny text path: simple framebuffer/fbcon, custom text console, or a second-core HSTX helper.
* UART remains available as fallback.
* Add a build-time option to enable/disable HSTX console.

## Milestone F: USB keyboard

* Start with boot-protocol HID keyboard only.
* First useful test: typed characters appear in the Linux shell.
* Use `fruitjam-usbhost wait`/`monitor` for line-state smoke checks and
  `fruitjam-usbhost reset` through `/dev/fruitjam-usbhost` for the first
  kernel-owned bus-reset primitive while PIO packet I/O is developed.
* Keep USB host on PIO2. PIO0 is used by NeoPixels and PIO1 is used by the
  current audio helper, while Pico-PIO-USB needs one complete PIO block for the
  TX/RX/EOP state machines and all 32 instruction words.
* The bridge now stages the 32-word full-speed host PIO program and reports
  `pio_ready`; this is the packet-engine landing zone, not HID enumeration yet.
* Keep the next step Linux-side and minimal: add PIO packet send/receive to the
  kernel bridge, then poll one boot-protocol keyboard endpoint.
* Do not include mouse, storage, arbitrary hub hotplug, or composite-device support in the first keyboard milestone.

## Milestone G: AirLift networking

The onboard ESP32-C6 AirLift runs NINA firmware and communicates with the RP2350
over RP2350 PL022/spidev hardware SPI. The first Linux milestone is a small
userspace probe that can reset the C6, read the NINA firmware version, scan APs,
join a network, report DHCP addressing, open a basic TCP connection, and publish
a QoS 0 MQTT message through the coprocessor. The current helper also serves a
small inbound HTTP/telnet/FTP surface directly through AirLift sockets when WiFi
is configured on the SD card.

Acceptance test:

1. Boot the image and connect over UART or USB CDC.
2. Run `airliftctl fw` and confirm the ESP32-C6 responds.
3. Run `airliftctl join <ssid> <passphrase>`.
4. Run `airliftctl status` and confirm `status 3 connected`.
5. Run `airliftctl ip` and confirm a DHCP address, netmask, and gateway.
6. Run `airliftctl tcp-get example.com /` and confirm an HTTP 200 response.
7. Run `airliftctl mqtt-pub <broker-host> 1883 fruitjam/test hello` and confirm
   an MQTT broker receives the message.
8. With `/mnt/sd/fruitjam/wifi.conf` present, boot the image and confirm
   `airliftctl serve-inbound` exposes `http://<board-ip>/`, telnet on TCP/23,
   and passive FTP on TCP/21 plus a passive data port.

Status: userspace NINA socket milestone achieved on Fruit Jam hardware with the
installed NINA firmware reporting `3.3.0`. `airliftctl mqtt-pub` was verified
against a local MQTT broker after joining WiFi, and inbound HTTP/telnet/passive
FTP were verified from another host.

The NINA protocol uses big-endian 16-bit SPI framing for buffer lengths and port
parameters. Do not change this to little-endian; doing so makes TCP connect/send
fail even though firmware/status/IP commands still work.

A normal Linux `wlan0`/socket integration would require either a kernel netdev
driver or a fuller userspace bridge and is a larger follow-on task. Updating the
ESP32-C6 firmware remains separate from the Linux image work; the tested board
already had the Adafruit Fruit Jam NINA `3.3.0` firmware.
