# RP2350 no-MMU Linux Buildroot ports

This tree builds tiny Buildroot-based Linux images for RP2350 RISC-V (Hazard3)
boards. The original target is the Raspberry Pi Pico 2 / SparkFun Pro Micro RP2350
work; this branch adds an Adafruit Fruit Jam RP2350B bring-up target.

For a community getting-started walkthrough, see
[Linux on Fruit Jam](https://adafruit-playground.com/u/mikeysklar/pages/linux-on-fruit-jam)
by Mikey Sklar on Adafruit Playground.

## Quick start after flashing

Flash the Fruit Jam UF2, then use the image from a serial console first. On the
tested macOS host the USB CDC console appears as `/dev/cu.usbmodem1101`; the
hardware UART header remains available at 115200 8N1 on GPIO8/GPIO9.

```sh
screen /dev/cu.usbmodem1101 115200
```

Once logged in, these commands show what the image can do:

```sh
fruitjam-services status
ls /root/berry /root/sh /root/rtttl
sh /root/sh/run-all.sh
berry-run /root/berry/run-all.be
berry-run /root/berry/run-visual.be
fruitjam-rtttl scale
```

The examples are intentionally part of the image:

* `/root/berry` contains Berry language and hardware examples, including the
  importable `fruitjam` hardware module, NeoPixel animations, USB HID decode,
  and direct sysfs/ADC/button reads.
* `/root/sh` contains shell examples for services, HTTP/CGI, NeoPixels, buttons,
  I2C, ADC, SD, DVI, USB-host status, WAV analysis, and small utility checks.
* `/root/rtttl` contains RTTTL tunes for the TLV320DAC3100 audio bring-up path.

To make the board reachable from another machine over the onboard ESP32-C6
AirLift, put WiFi credentials on the SD card and reboot:

```sh
mkdir -p /mnt/sd/fruitjam
chmod 700 /mnt/sd/fruitjam
cat > /mnt/sd/fruitjam/wifi.conf <<'EOF'
SSID=your-ssid
PASSWORD=your-passphrase
EOF
chmod 600 /mnt/sd/fruitjam/wifi.conf
```

After AirLift joins WiFi, `airliftctl serve-inbound` exposes the user-facing
network surface directly through the coprocessor:

* `http://<board-ip>/` serves `/mnt/sd/www/index.html` for user pages.
* `http://<board-ip>/playground` opens the built-in hardware playground.
* `telnet <board-ip> 23` opens the tiny Fruit Jam shell.
* Passive FTP on `<board-ip>:21` serves the SD card rooted at `/mnt/sd`, so
  `/wavs` is visible to FTP clients.

This is not a Linux `wlan0` interface. It is a small AirLift/NINA userspace
server for HTTP, telnet, and FTP; normal Linux sockets and the target-side
`wget` still use the local loopback network.

## Adafruit Fruit Jam RP2350B milestone

The Fruit Jam target is named `adafruit_fruit_jam_rp2350`. It uses:

* RV32 RISC-V Hazard3, no MMU.
* SMP disabled.
* BusyBox-centered userspace plus small no-MMU-friendly Fruit Jam tools.
* CramFS root filesystem in flash, with a 10 MiB flash partition reserved from
  6 MiB through the end of the 16 MiB device.
* Kernel copied to 8 MiB external PSRAM.
* UART1 console on the Fruit Jam `TX`/`RX` pins (GPIO8/GPIO9), 115200 8N1.
* USB CDC ACM console on `/dev/ttyGS0` when the gadget enumerates.
* USB CDC shells use standalone `/usr/bin/hush`. The hardware UART uses
  `fruitjam-uart-login` to wait for Enter before execing `hush`, avoiding a
  no-MMU respawn loop when nothing is attached to GPIO8/GPIO9. Telnet uses a
  tiny `fruitjam-telnetd` plus `/usr/bin/fruitjam-shell` so remote login does
  not require a large shell allocation, with a four-entry command history and
  command-name tab completion for telnet sessions.
* Berry installed as `/usr/bin/berry`, including `-e`, script execution, and REPL.
* BusyBox `vi`.
* PIO-backed `/dev/neopixels` for the five onboard NeoPixels, plus
  `/root/berry/neopixels.be` as a small Berry smoke-test program.
* Legacy sysfs GPIO through `gpiochip0` for GPIO0..GPIO45, including GPIO29 LED
  output and GPIO0/GPIO4/GPIO5 button inputs.
* microSD as `/dev/mmcblk0`/`/dev/mmcblk0p1`, mounted at `/mnt/sd` as VFAT.
* `fruitjam-buttons` daemon watching GPIO0/GPIO4/GPIO5 and logging events to
  `/mnt/sd/fruitjam/buttons.db`.
* `fruitjam-buttonlog`, a tiny fixed-schema SQLite 3 file writer. It avoids the
  full SQLite CLI/library because the no-MMU target cannot reliably allocate a
  large SQLite-linked process after services are running.
* Tiny `mosquitto_pub` compatible MQTT publisher. It uses normal Linux sockets
  by default, and `--airlift` routes the same publish request through
  `airliftctl mqtt-pub` over the ESP32-C6 NINA TCP socket API.
* GPIO-backed Linux I2C master on GPIO20/GPIO21, exposed as `/dev/i2c-0`, plus
  the tiny `fruitjam-i2c` scan/ping helper.
* `/dev/fruitjam-audio` starts/stops PIO1-generated TLV320DAC3100 MCLK, BCLK,
  WS, and DIN signals on GPIO24-GPIO27. `fruitjam-rtttl` is the current audio
  bring-up helper and has microphone-confirmed audible RTTTL output.
* `fruitjamctl` GPIO diagnostics for USB-host power enable, shared
  TLV320/ESP32-C6 reset, and software BOOTSEL.
* `airliftctl` diagnostics for the onboard ESP32-C6 AirLift NINA coprocessor,
  including firmware/status/IP checks and a minimal outbound TCP HTTP fetch.
  AirLift SPI access is locked so a second `airliftctl` exits safely while the
  inbound service owns the coprocessor; read-only `fw`, `mac`, `status`, and
  `ip` commands fall back to the last values in `/tmp/airlift-start.log`.

Validated on hardware:

* USB CDC shell on `/dev/cu.usbmodem1101`.
* Hardware UART shell/log path on `/dev/cu.usbserial-P97cvdxp`.
* `/proc/mtd` reports `mtd0: 00a00000 00001000 "cramfs"`.
* `/sys/class/gpio/gpio29/value` reads back output writes, and GPIO0/GPIO4/GPIO5
  export as pulled-up inputs.
* AirLift inbound HTTP serves user pages from `/mnt/sd/www` at
  `http://<board-ip>/` and the built-in hardware playground at
  `http://<board-ip>/playground` after WiFi is configured on the SD card.
* The optional loopback `fruitjam-services core` set can serve
  `http://127.0.0.1/`, `http://127.0.0.1/playground`, and CGI scripts; the tiny
  HTTP-only `wget -O -` implementation returns the CGI environment test.
* `fruitjam-services init` creates `/mnt/sd/www/index.html` with a small
  "Fruit Jam stuff" placeholder only when the SD card does not already have an
  index page. Users can replace that file with their own web page.
* The `/playground` page is a self-contained reactive Fruit Jam hardware
  playground. `/cgi-bin/fruitjam.cgi` exposes JSON actions for status,
  NeoPixels, button test events, RTTTL playback, WAV listing/playback, ADC
  reads, DVI rendering, USB-host status, I2C checks, and explicit Berry example
  runs. Hardware actions use direct C/tiny-helper paths; Berry is only invoked
  by the Berry runner.
* AirLift inbound telnet accepts a remote shell and echoed `TELNET_OK`.
* Default AirLift startup is wrapped by `fruitjam-services airlift-monitor`,
  which reruns the inbound server if it exits after leaving ESP32-C6 TCP sockets
  open. Check `/tmp/airlift-start.log` for probe/join and restart messages.
* `fruitjam-services status` reports service processes and TCP/UDP listeners
  without spawning `ps`/`netstat`, avoiding the no-MMU fragmentation regression
  that previously broke CGI after status checks.
* AirLift inbound passive FTP lists, uploads, and downloads files under
  `/mnt/sd`. The optional loopback core service set also includes tiny FTP and
  TFTP daemons for target-side smoke tests.
* `/proc/partitions` reports `mmcblk0` and `mmcblk0p1`; `/mnt/sd` is VFAT,
  writable, unmountable, and remountable.
* Synthetic button events written to `/run/fruitjam-buttons.fifo` are logged to
  `/mnt/sd/fruitjam/buttons.db` and mirrored in `/mnt/sd/fruitjam/buttons.log`.
* With `/mnt/sd/fruitjam/buttons.conf` set to `MQTT_TRANSPORT=airlift`, a
  synthetic button event published through `mosquitto_pub --airlift` and was
  received by a test MQTT broker as `fruitjam/buttons/button2`.
* `/dev/i2c-0` is `i2c-gpio0` on GPIO20/GPIO21; `fruitjam-i2c scan` found the
  TLV320DAC3100 control interface at `0x18`.
* `/sys/class/misc/fruitjam-audio` registers as misc char device `10:125`, and
  `/dev/fruitjam-audio` accepts `start`/`stop`; `fruitjam-rtttl --loud --tone
  880 2500` was verified with the Mac microphone helper.
* `berry -e 'print("berry ok")'`.
* Berry REPL expression evaluation.
* `berry-run /root/berry/neopixels.be`, visually confirmed on the onboard NeoPixels.
* `/bin/vi -> busybox`.
* `fruitjamctl bootsel`, verified by `picotool info -a` reporting
  `boot type: bootsel`.
* AirLift over RP2350 PL022/spidev hardware SPI:
  `airliftctl probe` reported NINA firmware `3.3.0`, WiFi joined successfully,
  and `airliftctl tcp-get example.com /` returned HTTP 200 plus the response
  body. `airliftctl mqtt-pub` published a QoS 0 MQTT message over the same NINA
  TCP path to a local broker, which received `fruitjam/test hello-from-airlift`.

AirLift is exposed as a userspace NINA socket helper, not as a Linux `wlan0`
network interface. The boot service starts `airliftctl serve-inbound` for
external HTTP, telnet, and FTP when WiFi is available. The tiny target-side
`wget` still uses normal Linux sockets, while `airliftctl tcp-get` and
`mosquitto_pub --airlift` use the ESP32-C6 directly.

### Fruit Jam hardware support matrix

Status means support in this Buildroot Linux image, not support in
CircuitPython. Pin details come from the Adafruit Fruit Jam pinout and the local
Fruit Jam pin map in [docs/pinmap-fruitjam.md](docs/pinmap-fruitjam.md).

| Hardware | Pins / device | Status | Current Linux support | TODO / notes |
| --- | --- | --- | --- | --- |
| RP2350B RISC-V CPU | Hazard3 RV32 | Supported | Boots Linux 6.15 no-MMU with SMP disabled. | Continue reducing memory pressure from larger userland tools. |
| External flash | 16 MiB QSPI | Supported | Bootloader, kernel, DTB, and 10 MiB CramFS rootfs live in flash. | None for current boot path. |
| External PSRAM | 8 MiB QMI/CS1 GPIO47 | Supported | Kernel is copied to PSRAM by the bootloader. | No general Linux RAM expansion beyond current port design. |
| USB device console | RP2350 USB CDC, `/dev/ttyGS0` | Supported | Host sees `/dev/tty.usbmodem1101`; CDC shell is used by the smoke suite. | CDC console can be sensitive to repeated open/close churn. |
| UART console header | GPIO8 TX, GPIO9 RX | Supported | Hardware UART shell/log path at 115200 8N1. | Keep as fallback when USB or graphics work changes. |
| ROM BOOTSEL from Linux | RP2350 reboot command | Supported | `fruitjamctl bootsel` re-enters BOOTSEL and `picotool info -a` sees the ROM. | None. |
| BusyBox userspace | `/bin`, `/usr/bin` | Supported | BusyBox tools plus `vi`, `hush`, web/network helpers, `free`/`fruitjam-mem`, and Fruit Jam tools. | Keep applets constrained for no-MMU allocation behavior. |
| Berry interpreter | `/usr/bin/berry`, `/usr/bin/berry-run`, `/root/berry/fruitjam.be` | Supported | `berry -e`, scripts, REPL, and an importable Fruit Jam hardware module for GPIO/buttons, ADC, USB-host status, USB HID report decode, device presence, audio clock, DVI command writes, and NeoPixels; `berry-run /root/berry/neopixels.be` drives LEDs with lower cache pressure for multi-script runs. | Extend the Berry module as more kernel helpers land. |
| Onboard NeoPixels | GPIO32, five LEDs | Supported | PIO-backed `/dev/neopixels`; Berry and CGI can update pixels. | None for basic color writes. |
| Buttons | GPIO0, GPIO4, GPIO5 | Supported | Sysfs GPIO input, `fruitjam-buttons`, button log, CGI status, synthetic test events. | Physical edge logging should get longer soak testing. |
| Red LED / IR pin | GPIO29 | Partial | LED control through sysfs/`fruitjamctl`; active-low output works. | IR receiver decoding is not implemented. |
| microSD card | GPIO34-39 SPI/MMC, CD GPIO33 | Supported | `/dev/mmcblk0p1` mounts at `/mnt/sd` as VFAT; read/write/remount verified. | Optional wider SDIO DAT1/DAT2 path is not implemented. |
| I2C / STEMMA QT | GPIO20 SDA, GPIO21 SCL | Supported | `i2c-gpio0` as `/dev/i2c-0`; `fruitjam-i2c scan` finds TLV320 at `0x18`. | Add kernel descriptions for more attached devices as needed. |
| ADC / analog header | GPIO40-GPIO47 ADC | Supported | `fruitjam-adc` reads ADC channels and internal temperature through sysfs. | Calibration/scale polish. |
| TLV320DAC3100 audio | I2C `0x18`, GPIO24 DIN, GPIO25 MCLK, GPIO26 BCLK, GPIO27 WS | Partial | `/dev/fruitjam-audio` generates PIO clocks; `fruitjam-rtttl` configures the codec and microphone-verified RTTTL output works. | Full streamed PCM/I2S playback is not implemented. |
| ESP32-C6 AirLift | SPI GPIO28/30/31/46, READY GPIO3, IRQ GPIO23, reset GPIO22 | Partial | NINA firmware `3.3.0`; `airliftctl` can probe, scan, join WiFi, TCP GET, MQTT publish, and serve inbound HTTP/telnet/FTP. | Not a Linux `wlan0` interface yet; normal `wget` still only uses Linux sockets. |
| HTTP user pages and playground | AirLift TCP/80; optional loopback HTTP | Supported | `/` serves `/mnt/sd/www/index.html`; `/playground` serves the built-in hardware UI; `/cgi-bin/fruitjam.cgi` reports status and controls NeoPixels, RTTTL, WAVs, I2C, ADC, DVI, USB-host status, buttons, and explicit Berry examples. Hardware actions use direct C/tiny-helper paths. | Use `http://<board-ip>/playground` after AirLift joins WiFi, or `fruitjam-services core` for loopback tests. |
| Telnet service | AirLift TCP/23; optional loopback TCP/23 | Supported | AirLift inbound shell and tiny `fruitjam-telnetd`/`fruitjam-shell`; telnet smoke tests pass. | Only one AirLift telnet session at a time. |
| FTP service | AirLift TCP/21 plus passive data 2121+; optional loopback TCP/21 | Supported | Passive FTP lists, uploads, and downloads files under `/mnt/sd`; FileZilla passive mode works. | Upload completion can be slow over the current NINA raw socket path; active FTP remains a future objective. |
| TFTP service | Optional loopback UDP/69 | Supported | BusyBox `tftpd` serves the TFTP area README when `fruitjam-services core` is started. | Not part of the default external AirLift service set. |
| USB host data | GPIO1 D+, GPIO2 D- | Experimental | USB host 5V can be switched, and `/dev/fruitjam-usbhost` owns line state, bus-reset timing, PIO2 packet I/O, descriptor/HID decode diagnostics, a narrow `kbd-init`/`kbd-poll` boot-keyboard probe path, and `kbd-text`/`kbd-events` polling loops for one direct boot keyboard. This is not a hub/composite/general USB stack. | Hardware-smoke the live boot-keyboard text/events path and widen endpoint/config parsing after it is stable. |
| USB host 5V switch | GPIO11 | Partial | `fruitjamctl usb-power on/off` controls power enable. | Needs full USB host stack for devices. |
| DVI / HSTX output | GPIO12-GPIO19, `/dev/fruitjam-dvi` | Partial | Tiny RGB332 HSTX DVI misc driver plus `fruitjam-dvi` text/dashboard/helper command output; hardware commands were verified on the flashed image. | Full fbdev/console is not implemented. |
| I2S data path | GPIO24 DIN, GPIO25 MCLK | Partial | Tiny generated-tone path exists for RTTTL and simple WAV tone tests. | Add complete streamed PCM driver/path. |
| IR receiver | GPIO29 shared with LED | TODO | No decoder. | Decide conflict/ownership with red LED. |
| ESP32-C6 native flashing from Linux | SPI/UART control path | TODO | Updated externally using Adafruit updater workflow. | Integrate a controlled in-image updater later. |

### Fruit Jam tool quick reference

The target keeps tools intentionally small because this is a no-MMU system where
process size and memory fragmentation matter.

| Tool or path | Purpose |
| --- | --- |
| `fruitjam-services` | Start, stop, restart, and inspect Fruit Jam services without large `ps`/`netstat` helpers. |
| `fruitjam-telnetd` | Tiny TCP/23 telnet shell service. |
| `fruitjam-shell` | Tiny command shell used by telnet sessions, with recent command history and command-name tab completion. |
| `fruitjam-ftpd` | Tiny FTP server rooted at `/mnt/sd`, so `/wavs` is visible to FTP clients. |
| BusyBox `httpd` | Local web server and CGI runner under `/www`. |
| `/cgi-bin/fruitjam.cgi` | JSON hardware playground API using direct C/tiny-helper hardware actions, plus an explicit Berry runner. |
| BusyBox `tftpd` | TFTP server rooted at `/tmp/tftp`. |
| `wget` | Tiny HTTP-only client for normal Linux IPv4 sockets. |
| `telnet` | Tiny telnet client. |
| `nc` | Tiny bidirectional netcat-style TCP client/server. |
| `/root/sh/serial-over-tcp.sh` | Bridges a character device to a TCP port with `nc`. |
| `fruitjamctl` | Board diagnostics, reset controls, USB-host power, and software BOOTSEL. |
| `fruitjam-i2c` | Scan or ping the GPIO-backed `/dev/i2c-0` bus. |
| `fruitjam-adc` | Read RP2350 ADC inputs and the internal temperature ADC channel. |
| `fruitjam-dvi` | Render bounded text/dashboard/test frames to `/dev/fruitjam-dvi`. |
| `fruitjam-wavplay` | Analyze simple WAV files and play tone segments through the TLV320 tone path. |
| `fruitjam-usbhost` | Report USB host power and D+/D- line state, preferring `/dev/fruitjam-usbhost` when present, with `json`, `wait`, `monitor`, `reset`, `decode`, `hid`, and experimental `kbd-init`/`kbd-poll` plus `kbd-text`/`kbd-events` boot-keyboard commands. |
| `fruitjam-hidkeys` | Decode USB HID boot-keyboard 8-byte reports into text/events, including DATA0/DATA1 `last_rx_hex` packets from the PIO bridge when they contain an 8-byte keyboard report. |
| `fruitjam-mem`, `free` | Tiny no-fork memory, uptime, load, and commit-pressure summary from `/proc`. |
| `fruitjam-buttons` | Button daemon for GPIO0/GPIO4/GPIO5 with log, FIFO, SQLite, and MQTT hooks. |
| `fruitjam-buttonlog` | Dump the fixed-schema button SQLite 3 log. |
| `fruitjam-rtttl` | Play a small RTTTL tune through the TLV320 PIO I2S tone path. |
| `airliftctl` | ESP32-C6 NINA firmware, WiFi, TCP, and MQTT diagnostics over SPI. |
| `mosquitto_pub` | Tiny MQTT publisher; add `--airlift` to publish through the ESP32-C6. |
| `berry` | Berry interpreter with `-e`, scripts, and REPL. |
| `berry-run` | Tiny wrapper that drops page cache before execing Berry for steadier multi-script runs. |
| `/dev/neopixels` | PIO-backed device for the five onboard NeoPixels. |
| BusyBox `vi` | On-target text editor. |

`/` is a read-only CramFS root. Use `/tmp` for temporary files and `/mnt/sd` for
persistent data. The SD card is mounted as VFAT at `/mnt/sd` when present.

### Host-side CDC smoke test

From macOS, run the automated smoke suite against the USB CDC Linux console:

```sh
./scripts/cdc-smoke-test.py
```

The default port is `/dev/tty.usbmodem1101`; override it with `--port` or
`FJ_CDC_PORT`. The suite keeps one CDC shell open and checks Berry, board
helpers, buttons, I2C, NeoPixels, loopback HTTP/Telnet/TFTP/FTP, and AirLift.

WiFi join and AirLift TCP tests run when credentials are supplied with either
`--ssid`/`--password`, environment variables, or the ignored local env file:

```sh
cat > .fruitjam.env <<'EOF'
export FJ_WIFI_SSID=your-ssid
export FJ_WIFI_PASSWORD=your-passphrase
EOF
chmod 600 .fruitjam.env
./scripts/cdc-smoke-test.py
```

Use `--audio` to include the short RTTTL speaker test. Use `--skip-airlift` or
`--skip-services` to narrow the suite during bring-up.

Recent release-prep verification on June 12, 2026:

```sh
./scripts/cdc-smoke-test.py
./scripts/validate-fruitjam-examples.sh
./scripts/validate-fruitjam-image.sh buildroot-output-docker-images
```

The CDC suite is the broad regression entrypoint. Release-prep hardware checks
also included direct AirLift HTTP, remote telnet, passive FTP list/upload/download
and microphone-verified RTTTL/WAV audio on the flashed image.

| Smoke test | Result | Evidence |
| --- | --- | --- |
| USB CDC shell | PASS | `fruitjam-cdc-ok` over `/dev/tty.usbmodem1101`. |
| Kernel boot | PASS | Linux 6.15.0 no-MMU Buildroot console responded. |
| Tool inventory | PASS | `airliftctl`, `berry`, `fruitjamctl`, `fruitjam-i2c`, `fruitjam-rtttl`, `fruitjam-services`, `fruitjam-buttons`, `fruitjam-usbhost`, `fruitjam-hidkeys`, `fruitjam-mem`, `free`, `wget`, `telnet`, `nc`, `tftp`, and `vi` found in `PATH`. |
| Berry expression | PASS | `berry -e 'print("berry-cdc-ok")'` printed `berry-cdc-ok`. |
| Board status helper | PASS | `fruitjamctl status` reported LED, USB power, peripheral reset, and three buttons. |
| Button helper | PASS | `fruitjam-buttons status` reported button1/button2/button3 released. |
| I2C scan | PASS | `fruitjam-i2c scan` found device `0x18`. |
| NeoPixel device | PASS | `printf 'clear\nwrite\n' > /dev/neopixels` completed. |
| AirLift probe | PASS | `airliftctl probe` reported spidev, firmware, MAC, status, and IP fields. |
| AirLift scan | PASS | `airliftctl scan` found visible networks. |
| AirLift WiFi join | PASS | Joined configured WiFi credentials and printed status/IP progress. |
| AirLift TCP GET | PASS | `airliftctl tcp-get example.com /` returned `HTTP/1.1 200 OK`. |
| Phase reboot | PASS | Test suite used `fruitjamctl bootsel` then `picotool reboot -a -c riscv` before service tests. |
| Service status | PASS | `fruitjam-services status` returned service/listener status without spawning large helpers. |
| HTTP CGI | PASS | AirLift HTTP returned `mode":"airlift-direct"` status JSON; loopback CGI is available through `fruitjam-services core`. |
| Telnet | PASS | AirLift telnet printed `Fruit Jam telnet shell` and echoed `TELNET_SPEED_OK`. |
| TFTP loopback | PASS | TFTP GET returned `Fruit Jam TFTP area` when the core loopback services were started. |
| FTP passive | PASS | Passive FTP over AirLift listed `/mnt/sd`, uploaded a file, and downloaded it with matching checksum. |
| RTTTL audio | PASS | `scripts/fruitjam-audio-mic-test.py --serial-port /dev/cu.usbserial-P97cvdxp --mic-device 1 --loud --tone-hz 880 --tone-ms 2500` matched the 880 Hz tone from the speaker. |

### Working examples

These examples are meant to be run from the Fruit Jam console over USB CDC,
UART, or telnet. They cover the features that have been brought up so far.

1. Check the default boot services and listeners:

   ```sh
   fruitjam-services status
   ```

   Expected services include the AirLift inbound worker and
   `fruitjam-buttons daemon`. External HTTP, telnet, and FTP are served by the
   AirLift worker after WiFi joins.

2. Start the optional loopback service set for target-side HTTP/TFTP/FTP tests:

   ```sh
   fruitjam-services core
   ```

   This starts `fruitjam-httpd`, `fruitjam-telnetd`, `fruitjam-ftpd`, and TFTP
   on the local Linux network stack. It is useful for smoke tests from the
   console; the default boot path keeps these larger daemons out of memory until
   requested.

3. Fetch the SD-card placeholder page through the tiny HTTP client:

   ```sh
   wget -O - http://127.0.0.1/index.html
   ```

4. Run the CGI environment smoke test:

   ```sh
   wget -O - http://127.0.0.1/cgi-bin/env.cgi
   ```

   Expected output starts with `Fruit Jam CGI OK`.

5. Open the hardware playground:

   ```sh
   wget -O - http://127.0.0.1/playground
   wget -O - http://127.0.0.1/cgi-bin/fruitjam.cgi?action=status
   ```

   In a browser, visit `http://<board-ip>/playground` after AirLift joins WiFi.
   The page has status tiles, NeoPixel color pickers, button tests, RTTTL
   playback, WAV listing/playback, DVI helpers, USB-host status, Berry example
   controls, ADC reads, and I2C checks.

6. Drive NeoPixels through HTTP CGI using direct `/dev/neopixels` writes:

   ```sh
   wget -O - 'http://127.0.0.1/cgi-bin/fruitjam.cgi?action=neopixels&c0=%23ff0000&c1=%23ffaa00&c2=%2300ff66&c3=%230088ff&c4=%23aa44ff'
   ```

   Expected JSON includes `"message":"neopixels updated"`. The web page no
   longer uses Berry for this fast hardware path; Berry is only invoked by the
   explicit Berry example runner.

7. Play the default RTTTL tune through the web API:

   ```sh
   wget -O - 'http://127.0.0.1/cgi-bin/fruitjam.cgi?action=rtttl'
   ```

8. Scan I2C and read ADC through the web API:

   ```sh
   wget -O - 'http://127.0.0.1/cgi-bin/fruitjam.cgi?action=i2c'
   wget -O - 'http://127.0.0.1/cgi-bin/fruitjam.cgi?action=adc&channel=temp'
   ```

9. Exercise the telnet shell over loopback:

   ```sh
   (echo 'echo TELNET_OK'; echo status; echo exit) | telnet 127.0.0.1 23
   ```

   Expected output includes `Fruit Jam telnet shell` and `TELNET_OK`.
   Interactive telnet sessions also support up/down history recall and tab
   completion for builtins and commands in `/bin`, `/usr/bin`, `/sbin`, and
   `/usr/sbin`.

10. Check the FTP control path:

   ```sh
   (echo USER anonymous; echo PASS x; echo PWD; echo TYPE I; echo QUIT) | nc 127.0.0.1 21
   ```

   Expected output includes `220 Fruit Jam FTP ready`, `230 Login ok`, and
   `221 Bye`.

11. Fetch the TFTP README into `/tmp`:

   ```sh
   tftp -g -r README.txt -l /tmp/tftp-readme.txt 127.0.0.1
   cat /tmp/tftp-readme.txt
   ```

   Expected output is `Fruit Jam TFTP area`.

12. Use `nc` as a tiny TCP server and client:

   ```sh
   echo SERVERDATA > /tmp/nc-send.txt
   : > /tmp/nc-recv.txt
   nc -l -p 7008 < /tmp/nc-send.txt > /tmp/nc-recv.txt &
   echo CLIENTDATA | nc 127.0.0.1 7008
   cat /tmp/nc-recv.txt
   ```

   Expected client output is `SERVERDATA`; the final `cat` should show
   `CLIENTDATA`.

13. Bridge a serial device over TCP:

   ```sh
   /root/sh/serial-over-tcp.sh 7000 /dev/ttyGS0
   ```

   In another shell, connect with `nc 127.0.0.1 7000`. With only the current
   userspace NINA helper there is no Linux `wlan0`, so remote access to this port
   needs a future Linux network interface or bridge.

14. Toggle the GPIO29 LED through sysfs:

   ```sh
   echo 29 > /sys/class/gpio/export 2>/dev/null || true
   echo out > /sys/class/gpio/gpio29/direction
   echo 1 > /sys/class/gpio/gpio29/value
   cat /sys/class/gpio/gpio29/value
   echo 0 > /sys/class/gpio/gpio29/value
   ```

15. Toggle a female-header GPIO, for example GPIO6:

    ```sh
    echo 6 > /sys/class/gpio/export 2>/dev/null || true
    echo out > /sys/class/gpio/gpio6/direction
    echo 1 > /sys/class/gpio/gpio6/value
    cat /sys/class/gpio/gpio6/value
    echo 0 > /sys/class/gpio/gpio6/value
    ```

16. Inspect the three onboard buttons:

    ```sh
    fruitjam-buttons status
    ```

    GPIO0 is button 1, GPIO4 is button 2, and GPIO5 is button 3.

17. Inject a synthetic button event and inspect the SQLite log:

    ```sh
    echo 'button2 gpio4 test 123' > /run/fruitjam-buttons.fifo
    fruitjam-buttonlog dump /mnt/sd/fruitjam/buttons.db 5
    tail -n 5 /mnt/sd/fruitjam/buttons.log
    ```

18. Write, unmount, remount, and read the SD card:

    ```sh
    echo fruitjam_sd_audit > /mnt/sd/audit.txt
    umount /mnt/sd
    mount /mnt/sd
    cat /mnt/sd/audit.txt
    ```

19. Read ADC channels:

    ```sh
    fruitjam-adc read 0
    fruitjam-adc read temp
    ```

20. Scan or ping the I2C bus:

    ```sh
    fruitjam-i2c scan
    fruitjam-i2c ping 0x18
    ```

    The TLV320DAC3100 control interface should acknowledge at `0x18`.

21. Start the TLV320 clock path and run the RTTTL audio bring-up helper:

    ```sh
    echo start > /dev/fruitjam-audio
    fruitjam-rtttl --loud --tone 880 2500
    echo stop > /dev/fruitjam-audio
    ```

    Audible output was verified from the host with
    `scripts/fruitjam-audio-mic-test.py`. WAV playback exists through
    `fruitjam-wavplay`, but SD-card WAV files should be treated separately from
    the audio path when the kernel reports FAT read errors.

    To install and verify the generated sample WAV at
    `/mnt/sd/wavs/fruitjam-scale.wav`:

    ```sh
    scripts/fruitjam-audio-mic-test.py \
      --serial-port /dev/cu.usbserial-P97cvdxp \
      --install-sd-wav --run-wavplay --loud --rtttl scale
    ```

22. Run Berry expressions, scripts, and the REPL:

    ```sh
    berry -e 'print("hello fruit jam")'
    berry -e 'import math print(math.sqrt(81))'
    berry-run /root/berry/neopixels.be
    berry
    ```

    `berry-run /root/berry/neopixels.be` should drive the five onboard NeoPixels.

23. Inspect AirLift firmware, join WiFi, and perform an HTTP fetch through the
    ESP32-C6:

    ```sh
    airliftctl fw
    airliftctl join SSID PASSWORD
    airliftctl status
    airliftctl ip
    airliftctl tcp-get example.com /
    ```

    For automatic AirLift service join on the board, put credentials on the SD
    card instead of in the rootfs overlay:

    ```sh
    mkdir -p /mnt/sd/fruitjam
    chmod 700 /mnt/sd/fruitjam
    cat > /mnt/sd/fruitjam/wifi.conf <<'EOF'
    SSID=your-ssid
    PASSWORD=your-passphrase
    EOF
    chmod 600 /mnt/sd/fruitjam/wifi.conf
    ```

    Do not put real WiFi credentials in commits, image overlays, or public logs.

24. Publish MQTT through AirLift:

    ```sh
    mosquitto_pub --airlift -h 192.0.2.10 -p 1883 -t fruitjam/test -m hello-from-fruitjam
    ```

    Replace the broker address with your own MQTT broker.

25. Route button events to MQTT through the SD-card config:

    ```sh
    mkdir -p /mnt/sd/fruitjam
    printf '%s\n' \
      MQTT_ENABLE=1 \
      MQTT_TRANSPORT=airlift \
      MQTT_HOST=192.0.2.10 \
      MQTT_PORT=1883 \
      MQTT_TOPIC_PREFIX=fruitjam/buttons \
      > /mnt/sd/fruitjam/buttons.conf
    fruitjam-services restart
    echo 'button2 gpio4 test 223' > /run/fruitjam-buttons.fifo
    ```

    `fruitjam-buttons` reads `/etc/fruitjam-buttons.conf` first and
    `/mnt/sd/fruitjam/buttons.conf` second at daemon startup, so restart services
    after changing the SD-card config.

26. Re-enter software BOOTSEL without pressing the physical button:

    ```sh
    fruitjamctl bootsel
    ```

    This disconnects the running system and should make the RP2350 BOOTSEL volume
    appear on the host.

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
