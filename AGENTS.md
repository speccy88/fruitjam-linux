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
* `/root/neopixels.be` lights the five onboard NeoPixels.
* `fruitjamctl bootsel` reboots into the RP2350 ROM BOOTSEL loader, verified
  with `picotool info -a`.

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

## Smoke Tests

USB CDC shell smoke test:

```sh
python3 - <<'PY'
import time, serial
port = "/dev/cu.usbmodem1101"
cmds = [
    "echo CDC_OK",
    "berry -e 'print(\"berry ok\")'",
    "ls -l /usr/bin/berry /bin/vi /dev/neopixels /root/neopixels.be",
    "berry /root/neopixels.be",
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

## Coding Style

* Keep code small and explicit. The target has 8 MiB PSRAM and no MMU.
* Prefer board-specific Buildroot packages, config fragments, and Linux patches
  over large cross-tree refactors.
* Keep BusyBox/userspace minimal.
* Use static linking and no-MMU friendly assumptions.
* Do not add dynamic loading, readline, or heavyweight runtime dependencies to
  Berry without measuring memory impact.
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
