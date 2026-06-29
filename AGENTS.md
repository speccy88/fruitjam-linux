# Codex Project Instructions

This repository builds small Buildroot-based Linux images for RP2350 RISC-V
targets, especially the Adafruit Fruit Jam RP2350B.

## Current Fruit Jam Status

The active board target is `adafruit_fruit_jam_rp2350`.

Validated on Adafruit Fruit Jam hardware:

* RV32 no-MMU Linux boots on the Hazard3 RISC-V core.
* Hardware UART console works on GPIO8/GPIO9 at 115200 8N1.
* USB CDC ACM console works as `/dev/ttyGS0`.
* Berry is installed as `/usr/bin/berry`.
* Berry `-e`, script execution, and REPL work.
* BusyBox `vi` is enabled.
* `/dev/neopixels` works through the RP2350 PIO NeoPixel driver.
* `/root/berry/neopixels.be` lights the five onboard NeoPixels.
* BusyBox httpd/CGI and tftpd work on loopback; telnet, FTP, `nc`, `wget`, and
  status use tiny Fruit Jam helpers to stay below fragile no-MMU allocation
  classes.
* The flash rootfs is a 10 MiB CramFS partition from offset 6 MiB through the
  end of the 16 MiB flash device.
* Serial login shells use `/usr/bin/hush`; telnet uses `fruitjam-telnetd` and
  `/bin/sh` with BusyBox standalone applets.
* `fruitjam-services status` reads `/proc` directly and must not regress to
  spawning larger helper applets such as `ps` or `netstat`; CGI was verified
  before and after status/telnet checks.
* microSD mounts at `/mnt/sd`.
* GPIO29 works through `/sys/class/gpio`, and GPIO0/GPIO4/GPIO5 are watched by
  `fruitjam-buttons daemon`.
* Button events written to `/run/fruitjam-buttons.fifo` are logged to
  `/mnt/sd/fruitjam/buttons.db` by the tiny fixed-schema `fruitjam-buttonlog`
  SQLite 3 file writer. Do not re-enable the full SQLite CLI/library casually;
  it caused no-MMU contiguous allocation failures.
* GPIO20/GPIO21 are exposed as `/dev/i2c-0` via `i2c-gpio0`; `fruitjam-i2c scan`
  found the TLV320DAC3100 at `0x18`.
* `/dev/fruitjam-audio` is a misc char device from the `fruitjam-audio-clock`
  driver. It starts/stops PIO1-generated TLV320DAC3100 MCLK/BCLK/WS/DIN signals
  on GPIO24/GPIO25/GPIO26/GPIO27.
* `fruitjam-rtttl` configures the TLV320 over `/dev/i2c-0` and plays RTTTL
  through the PIO I2S tone path. Audible output was verified with the Mac
  microphone helper; exit-status-only tests are not sufficient for regressions.
* `fruitjamctl bootsel` reboots into the RP2350 ROM BOOTSEL loader, verified
  with `picotool info -a`.
* `airliftctl` talks to the onboard ESP32-C6 AirLift over RP2350 PL022/spidev
  hardware SPI. With installed NINA firmware `3.1.0`, WiFi join, DHCP IP
  reporting, and `airliftctl tcp-get example.com /` returning HTTP 200 were
  verified on hardware.
* `airliftctl mqtt-pub HOST PORT TOPIC MESSAGE [CLIENTID]` and
  `mosquitto_pub --airlift` provide a userspace AirLift MQTT QoS 0 publish path.
  Button MQTT can select it with `MQTT_TRANSPORT=airlift` in
  `/mnt/sd/fruitjam/buttons.conf`. This was verified with a local MQTT broker:
  direct `airliftctl mqtt-pub` delivered `fruitjam/test`, and a synthetic
  button FIFO event delivered `fruitjam/buttons/button2`.

## Hardware Ports

Known macOS device names from bring-up:

* Hardware UART: `/dev/cu.usbserial-P97cvdxp`, 115200 8N1.
* USB CDC console: `/dev/cu.usbmodem1101`, 115200 8N1.

Do not assume these names are permanent. Check `/dev/cu.usb*` when reconnecting.

Prefer `picotool` for flashing. Avoid relying on `/Volumes/RP2350`; macOS mass
storage mounts were flaky during bring-up.

## Build Rules

Do not use native macOS Buildroot for verification. Buildroot rejects Apple clang
as host `gcc`, and mixed macOS/Docker Buildroot output can leave wrong-host helper
binaries in `buildroot/output`.

Use the Docker Buildroot output volume:

```sh
docker run --rm \
  -v fruitjam-br-output:/br-output \
  -v "$PWD":/src \
  -w /src \
  debian:13 \
  bash -lc 'set -e; apt-get update >/dev/null; apt-get install -y --no-install-recommends ca-certificates patch git make binutils gcc file wget cpio unzip rsync bc bzip2 g++ perl python3 xz-utils genimage mtools dosfstools cmake >/dev/null; make -C buildroot O=/br-output BR2_EXTERNAL=/src -j$(nproc)'
```

When kernel patches or kernel config change, clean the kernel first:

```sh
docker run --rm \
  -v fruitjam-br-output:/br-output \
  -v "$PWD":/src \
  -w /src \
  debian:13 \
  bash -lc 'set -e; apt-get update >/dev/null; apt-get install -y --no-install-recommends ca-certificates patch git make binutils gcc file wget cpio unzip rsync bc bzip2 g++ perl python3 xz-utils genimage mtools dosfstools cmake >/dev/null; make -C buildroot O=/br-output BR2_EXTERNAL=/src linux-dirclean; rm -f /br-output/build/.root /br-output/build/.rootfs_* /br-output/images/rootfs.* /br-output/images/flash-image.bin /br-output/images/flash-image.uf2; make -C buildroot O=/br-output BR2_EXTERNAL=/src -j$(nproc)'
```

When bootloader code changes, also clean `pico2-bootloader`:

```sh
make -C buildroot O=/br-output BR2_EXTERNAL=/src pico2-bootloader-dirclean linux-dirclean
```

## Export and UF2 Conversion

Export Docker-built images to the local artifact directory:

```sh
docker run --rm \
  -v fruitjam-br-output:/br-output \
  -v "$PWD/buildroot-output-docker-images":/dest \
  alpine:latest \
  sh -lc 'mkdir -p /dest; cp -f /br-output/images/flash-image.bin /br-output/images/rootfs.tar /br-output/images/Image /br-output/images/adafruit_fruit_jam_rp2350.dtb /br-output/images/bootloader.bin /dest/'
```

Convert to UF2 on macOS:

```sh
picotool uf2 convert \
  buildroot-output-docker-images/flash-image.bin \
  buildroot-output-docker-images/flash-image.uf2 \
  --family rp2350-riscv
```

Record checksums:

```sh
shasum -a 256 buildroot-output-docker-images/flash-image.uf2
```

## Flashing

If the board is already in BOOTSEL:

```sh
picotool info -a
picotool load -fu buildroot-output-docker-images/flash-image.uf2
picotool reboot
```

If `picotool info -a` does not see a BOOTSEL device, use the physical BOOTSEL
button plus reset/power cycle.

When a shell is already available on the board, prefer software BOOTSEL:

```sh
/usr/bin/fruitjamctl bootsel
```

On macOS, open the UART with DTR/RTS disabled before sending that command.

## Smoke Tests

USB CDC shell smoke test:

```sh
python3 - <<'PY'
import time, serial
port = "/dev/cu.usbmodem1101"
cmds = [
    "echo CDC_OK",
    "berry -e 'print(\"berry ok\")'",
    "ls -l /usr/bin/berry /bin/vi /dev/neopixels /root/berry/neopixels.be",
    "berry-run /root/berry/neopixels.be",
    "echo SMOKE_DONE",
]
with serial.Serial(port, 115200, timeout=0.25, write_timeout=1) as s:
    s.dtr = True
    s.rts = False
    time.sleep(0.5)
    s.reset_input_buffer()
    for cmd in cmds:
        s.write((cmd + "\r\n").encode())
        s.flush()
        time.sleep(0.55)
    data = bytearray()
    end = time.time() + 10
    while time.time() < end:
        n = s.in_waiting
        if n:
            data.extend(s.read(n))
            end = time.time() + 1.0
        time.sleep(0.05)
print(data.decode("utf-8", "replace"))
PY
```

Hardware UART listener:

```sh
python3 - <<'PY'
import time, serial
port = "/dev/cu.usbserial-P97cvdxp"
with serial.Serial(port, 115200, timeout=0.2, write_timeout=1) as s:
    s.dtr = False
    s.rts = False
    time.sleep(0.2)
    data = bytearray()
    end = time.time() + 8
    while time.time() < end:
        n = s.in_waiting
        if n:
            data.extend(s.read(n))
            end = time.time() + 1.0
        time.sleep(0.05)
print(data.decode("utf-8", "replace"))
PY
```

Avoid large target-side pipelines such as `dmesg | grep | tail`; they can exhaust
contiguous memory on this no-MMU system.

Audio smoke test:

```sh
python3 - <<'PY'
import time, serial
port = "/dev/cu.usbserial-P97cvdxp"
cmds = [
    "cat /sys/class/misc/fruitjam-audio/dev",
    "ls -l /dev/fruitjam-audio /usr/bin/fruitjam-rtttl /dev/i2c-0",
    "fruitjam-i2c ping 0x18",
    "echo start > /dev/fruitjam-audio; echo AUDIOCLK_START:$?",
    "echo stop > /dev/fruitjam-audio; echo AUDIOCLK_STOP:$?",
    "/usr/bin/fruitjam-rtttl",
    "echo POST_RTTTL:$?",
]
with serial.Serial(port, 115200, timeout=0.25, write_timeout=1) as s:
    s.dtr = False
    s.rts = False
    time.sleep(0.5)
    s.reset_input_buffer()
    for cmd in cmds:
        s.write((cmd + "\n").encode())
        s.flush()
        time.sleep(5.5 if "fruitjam-rtttl" in cmd else 0.8)
    data = bytearray()
    end = time.time() + 8
    while time.time() < end:
        n = s.in_waiting
        if n:
            data.extend(s.read(n))
            end = time.time() + 1.0
        time.sleep(0.05)
print(data.decode("utf-8", "replace"))
PY
```

No-MMU allocation guardrails:

* Prefer tiny single-purpose C helpers or standalone BusyBox applets when a path
  runs while services are resident.
* Avoid adding target-side status commands that spawn `ps`, `netstat`, `which`,
  `/bin/sh`, or similar larger applets.
* Re-test CGI, telnet, `nc`, and `fruitjam-services status` after adding daemons
  or larger binaries; storage space and executable load-to-RAM pressure are
  separate constraints.
* After heavy AirLift WiFi/MQTT tests, reboot before drawing conclusions about
  steady-state service memory. The ESP32-C6 debug output and extra transient
  processes increased allocation pressure during bring-up; a clean reboot
  restored httpd, telnetd, FTP, TFTP, button daemon, and CGI.

## Coding Style

* Keep code small and explicit. The target has 8 MiB PSRAM and no MMU.
* Prefer board-specific Buildroot packages, config fragments, and Linux patches
  over large cross-tree refactors.
* Keep BusyBox/userspace minimal.
* Use static linking and no-MMU friendly assumptions.
* Do not add dynamic loading, readline, or heavyweight runtime dependencies to
  Berry without measuring memory impact.
* Do not use userspace `/dev/mem` for PIO, reset, pad, or IO_BANK0 audio paths.
  On this no-MMU target those accesses returned `EFAULT`; use tiny kernel
  helpers for shared RP2350 register windows.
* Keep generated build artifacts out of git.
* Do not commit `.DS_Store`.

## Repo Rules

* Use `rg` for searching.
* Use `apply_patch` for manual file edits.
* Do not revert unrelated dirty work unless explicitly asked.
* Stage intentionally; do not sweep local artifacts into commits.
* Keep `buildroot-output-docker-images/` local only.
* If moving the Fruit Jam DTS, keep `BR2_LINUX_KERNEL_INTREE_DTS_NAME` aligned.

## Release Checklist

1. Rebuild in Docker from the committed source tree.
2. Export `flash-image.bin`.
3. Convert `flash-image.uf2` with `picotool`.
4. Record SHA256.
5. Commit and push source changes.
6. Create a GitHub release tag.
7. Attach `flash-image.uf2`.
8. In release notes, state both what was hardware-tested and what remains
   incomplete.

Recommended release asset name:

```text
adafruit-fruitjam-rp2350-linux-<tag>.uf2
```

## AirLift Networking Notes

The onboard ESP32-C6 AirLift is not a normal Linux WiFi device yet. It runs NINA
firmware and communicates with RP2350 over SPI. The first useful Linux milestone
is a small probe that can reset the C6, read NINA firmware version, scan APs, join
a network, and open a TCP connection through the coprocessor. A normal `wlan0`
requires a kernel netdev driver or userspace network bridge.

Use `airliftctl` for current hardware smoke tests:

```sh
airliftctl fw
airliftctl status
airliftctl join <ssid> <passphrase>
airliftctl ip
airliftctl tcp-get example.com /
airliftctl mqtt-pub <broker-host> 1883 fruitjam/test hello
```

The NINA socket SPI protocol uses big-endian 16-bit framing for `sendBuffer`
lengths and for `sendParam(uint16_t)` port values. Keep TCP payload request
length prefixes big-endian; the HTTP fetch test fails with `TCP sent 0 ...` if
these are little-endian. Do not hardcode WiFi credentials in source or docs.
The ESP32-C6 firmware update to Adafruit's Fruit Jam NINA `3.3.0` binary is not
yet part of the Linux image.
