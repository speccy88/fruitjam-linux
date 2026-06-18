# Fruit Jam USB Host Device Notes

Collected on 2026-06-17 from macOS while debugging Fruit Jam RP2350 USB host
enumeration. The goal is to preserve the exact device facts for later Linux
PIO USB-host bring-up.

## Capture Commands

Useful commands on the Mac:

```sh
ioreg -p IOUSB -l -w0
ioreg -r -c IOUSBHostInterface -l -w0
ioreg -r -c AppleUserUSBHostHIDDevice -l -w0
ioreg -r -c IOSerialBSDClient -l -w0
hidutil list
system_profiler SPAudioDataType -json
system_profiler SPCameraDataType -json
```

`system_profiler SPUSBDataType -json` returned an empty `SPUSBDataType` list on
this Mac, so prefer `ioreg`, `hidutil`, or PyUSB for current evidence.

PyUSB was also available and gave the most compact descriptor dump:

```python
import usb.core, usb.util

for vid, pid, name in (
    (0x03f0, 0x584a, "HP mouse"),
    (0x046d, 0xc534, "Logitech receiver"),
    (0x046d, 0xc016, "Logitech optical mouse"),
    (0x05ac, 0x1006, "Apple keyboard hub"),
    (0x05ac, 0x0221, "Apple keyboard"),
    (0x0403, 0x6015, "Parallax PropPlug FTDI serial"),
    (0x0ac8, 0x301b, "Vimicro PC Camera"),
    (0x047f, 0xc012, "Plantronics .Audio 628 USB"),
    (0x045e, 0x0291, "Xbox 360 controller receiver"),
):
    dev = usb.core.find(idVendor=vid, idProduct=pid)
    print(name, dev)
    for cfg in dev:
        for intf in cfg:
            for ep in intf:
                direction = "IN" if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN else "OUT"
                print(intf.bInterfaceNumber, intf.bInterfaceClass,
                      intf.bInterfaceSubClass, intf.bInterfaceProtocol,
                      hex(ep.bEndpointAddress), direction,
                      ep.wMaxPacketSize, ep.bInterval)
```

## Wili8jam USB Host Configuration Port - 2026-06-17

Comparison source: `https://github.com/freewili/wili8jam`, local clone commit
`71104dda2ac448fe2eb6c0c62f988eb6154faa78`.

The relevant known-working wili8jam USB host configuration is:

```text
CMakeLists.txt
  PICO_BOARD adafruit_fruit_jam

src/main.cpp
  vreg_set_voltage(VREG_VOLTAGE_1_30)
  set_sys_clock_pll(1260000000, 5, 1)  # 252 MHz
  GPIO11 high for USB-A VBUS
  pio_cfg = PIO_USB_DEFAULT_CONFIG
  pio_cfg.pin_dp = 1                  # D+ GPIO1, D- GPIO2
  pio_cfg.tx_ch = 9
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg)

usb-host/tusb_config.h
  CFG_TUH_ENABLED 1
  CFG_TUH_RPI_PIO_USB 1
  BOARD_TUH_RHPORT 1
  CFG_TUH_MAX_SPEED OPT_MODE_FULL_SPEED
  CFG_TUSB_RHPORT1_MODE OPT_MODE_HOST | OPT_MODE_FULL_SPEED
  CFG_TUH_ENUMERATION_BUFSIZE 512
  CFG_TUH_HUB 1
  CFG_TUH_HID 4
  CFG_TUH_XINPUT 4
  CFG_TUH_DEVICE_MAX 4
  CFG_TUH_HID_EPIN_BUFSIZE 64
  CFG_TUH_HID_EPOUT_BUFSIZE 64

Pico-PIO-USB/src/pio_usb_configuration.h
  default PIO TX 0, SM TX 0, PIO RX 0, SM RX 1, SM EOP 2

Pico-PIO-USB/src/pio_usb.c
  D+/D- fast slew
  D+/D- 12 mA drive strength
```

Linux source changes under this test now match those hardware-side knobs:

```text
board/adafruit/.../adafruit_fruit_jam_rp2350.dts
  clk_sys fixed clock: 252000000
  usbhost pio: 0
  usbhost pio reg: 0x50200000
  usbhost sm-tx/sm-rx/sm-eop: 0/1/2
  usbhost D+/D-/power GPIOs: 1/2/11
  usbhost TX DMA channel: 9
  neopixels moved to PIO2 at 0x50400000 so USB can use PIO0

board/raspberrypi/.../0022-misc-configure-fruitjam-usbhost-pio2-tx-path.patch
  D+/D- pad setup: IE | PDE | DRIVE_12MA | SLEWFAST
```

Exported artifact checksums for the latest rebuilt source image:

```text
flash-image.bin sha256 a243fa26584fb8d72c89fcdfabad61cd9623e0a846da9338eef683e34aa1feec
flash-image.uf2 sha256 9a6b4ffc15a8d7e2bdde1a1ae6531873ba5605e1abf6bb62a83a05484eb0c004
Image sha256 f0fe038687432dde93de5380ed7441e76a7982db2a13129e7fc3fad9859cff2d
adafruit_fruit_jam_rp2350.dtb sha256 1f869eacec12611620a43b590632d39fc9771cdebf2b2c3995f6da7906eeb4ea
bootloader.bin sha256 beae9f2c0f163b761742f4160c6a4250cf358cb9305ece88708a9753e890bcbd
```

This image adds Linux USB/HID/input prerequisites:

```text
CONFIG_USB=y
CONFIG_USB_HID=y
CONFIG_HID=y
CONFIG_HID_GENERIC=y
CONFIG_INPUT=y
CONFIG_INPUT_EVDEV=y
CONFIG_INPUT_JOYDEV=y
CONFIG_INPUT_JOYSTICK=y
CONFIG_JOYSTICK_XPAD=y
CONFIG_FRUITJAM_USBHOST_BRIDGE=y
```

It also adds:

```text
0081-misc-match-pico-pio-usb-rp2350-line-read-workaround.patch
  Ports Pico-PIO-USB's RP2350 D+/D- line-read input-enable pulse into the
  Fruit Jam bridge status/speed-detection path.

0082-misc-register-fruitjam-usbhost-hcd.patch
  Registers a USB 1.1 root hub/HCD scaffold and routes synchronous control
  IN/no-data plus interrupt IN URBs through the existing PIO transaction
  helpers, so Linux USB core, usbhid, and xpad can attempt enumeration.

0083-misc-frame-pace-fruitjam-usbhost-hcd-transfers.patch
  Keeps the HCD path closer to TinyUSB/Pico-PIO-USB frame timing: send SOF
  before SETUP, retry the SETUP stage cleanly when the ACK is missing, send SOF
  before data/status stages, send SOF before interrupt IN, and use the endpoint
  maxpacket size for short-packet detection.

0084-misc-expand-fruitjam-usbhost-rx-capture.patch
  Expands the kernel RX capture buffer to 72 bytes so a full 64-byte
  full-speed data packet plus sync/PID/CRC can be received without truncation.
  `package/fruitjam-utils/src/fruitjam-usbhost.c` was updated to parse and
  display the matching 144-character RX hex string.
```

The first `0082` image, before the HCD retry update, was flashed successfully:

```text
flash-image.uf2 sha256 9a10cd0a04a5a7ce78ef9ecccf355bbaf2c7b590c6b7ad2a9365b03c7886a068
picotool load -fu buildroot-output-docker-images/flash-image.uf2
  The device was rebooted into application mode.
```

That image booted and registered the Linux root hub:

```text
uname -a
  Linux (none) 6.15.0 #1 Wed Jun 17 23:06:07 UTC 2026 riscv32

cat /sys/bus/usb/devices/usb1/product
  Fruit Jam RP2350 PIO USB host

cat /sys/bus/usb/devices/usb1/manufacturer
  Linux 6.15.0 fruitjam-usbhost

cat /sys/bus/usb/devices/usb1/speed
  12

cat /sys/bus/usb/devices/usb1/maxchild
  1

ls -l /sys/bus/usb/devices
  1-0:1.0
  usb1

ls -l /dev/input
  No such file or directory
```

The same boot still failed before downstream HID/XInput: `fruitjam-usbhost
status` reported full-speed line state and PIO0 configured, but no received
external packets from endpoint zero:

```text
usbhost power on gpio11=1 gpio1(dp)=1 gpio2(dm)=0
usbhost device full-speed-device
usbhost stack kernel bridge line-state; PIO host program staged; boot-keyboard init/poll/text/event path available
usbhost pio-ready yes
usbhost pio-configured yes
usbhost pio-debug index 0 sm-tx 0 sm-rx 1 sm-eop 2 clk-sys-hz 252000000
usbhost packets 237 tx-errors 4
usbhost rx-attempts 12 rx-errors 12 last-rx-result -110 pid 0x00 len 0
```

Do not use target-side `dmesg` on this no-MMU image. Plain `dmesg` tried to
allocate a 229376 byte buffer and failed with an order-6 page allocation
failure, and `dmesg -s 4096` later exited 139 after the same 229376 byte
allocation class appeared in the log. Prefer CDC boot-output captures and tiny
single-purpose status helpers.

The frame-paced HCD image through `0084` was rebuilt with `linux-dirclean`,
then rebuilt as a full Buildroot image and converted to UF2.
`./scripts/validate-fruitjam-image.sh buildroot-output-docker-images` passed
for this exported image and reported the UF2 SHA256 above. Later images through
`0094` were flashed and tested on hardware; the `0098` image below was rebuilt
and validated but is not yet flashed because the current board has not appeared
in ROM BOOTSEL.

After flashing an HCD image, use the tiny HCD smoke helper instead of ad hoc
`dmesg` or large shell pipelines:

```sh
./scripts/usbhost-hcd-smoke.py --port /dev/cu.usbmodem101 -v
```

This helper checks the shell, `fruitjam-usbhost status`, `usb1` root-hub sysfs
attributes, `/sys/bus/usb/devices`, `/dev/input`, and `/proc/bus/input/devices`.
It fails by default if no keyboard/gamepad input node appears; pass
`--allow-root-only` only when confirming root-hub registration without claiming
downstream USB input works.

## Latest HCD Endpoint-Zero Status - 2026-06-17

The `0090` image changed the HCD transaction path so RX starts after the token
phase. It still failed enumeration, but the Linux USB core reached normal
full-speed device attach attempts:

```text
usb 1-1: new full-speed USB device number 2 using fruitjam-usbhost
usb 1-1: device descriptor read/64, error -71
usb 1-1: device not accepting address 4, error -71
usb usb1-port1: unable to enumerate USB device
```

The `0091` image additionally matched Pico-PIO-USB's RX prepare/reset sequence
more closely by keeping the one-time RX OSR setup in program initialization
instead of reloading it for every receive. That image was flashed and booted.
`fruitjam-usbhost status` on that image showed the wili8jam/Pico-PIO-USB
electrical configuration still matched the target values:

```text
usbhost power on gpio11=1 gpio1(dp)=1 gpio2(dm)=0
usbhost device full-speed-device
usbhost pio-debug index 0 sm-tx 0 sm-rx 1 sm-eop 2 clk-sys-hz 252000000
usbhost pio-clkdiv tx 0x00054000 rx 0x00010000 eop 0x0002a000
usbhost dma present 1 channel 9 packets 13951 last-result 0 ctrl 0x00812013
usbhost rx-attempts 200 rx-errors 100 last-rx-result -71 pid 0x5b len 4
usbhost probe-summary hcd-control setup-capture tx-len=34 rx-len=4
usbhost gpio-regs dp-ctrl 0x00010006 dm-ctrl 0x00010006 dp-pad 0x00000077 dm-pad 0x00000077
usbhost last-rx-hex 015b0020
```

The `0091` failure was useful because `setup-capture tx-len=34 rx-len=4` and
`last-rx-hex 015b0020` showed the capture path was reading shifted host SETUP
traffic, not a valid device ACK or descriptor response. The first four expected
host SETUP token bytes are `802d0010`; the captured `015b0020` pattern is a
one-bit-shifted form of the host transmission. That matches the same class of
first-RX bit-shift issue seen in Pico-PIO-USB issue 97.

The `0092-misc-use-gated-setup-ack-for-fruitjam-usbhost-hcd.patch` image
switched the HCD SETUP stage away from stream capture and back to the gated ACK
helper so the host TX echo is not mistaken for device response data. That image
was flashed and did stop reporting the shifted host echo as a received packet,
but the onboard CH334F hub still did not ACK endpoint-zero SETUP:

```text
usbhost rx-attempts 100 rx-errors 100 last-rx-result -110 pid 0x00 len 0
usbhost probe-summary hcd-control setup-arm-ack tx-len=50
```

The `0094-misc-shrink-fruitjam-usbhost-hcd-ack-arm-gap.patch` image reduced
the post-DATA0 ACK-arm gap from 16 bytes to 4 bytes. It was flashed and booted.
The expected `tx-len` changed, but the external bus result did not:

```text
usbhost power on gpio11=1 gpio1(dp)=1 gpio2(dm)=0
usbhost device full-speed-device
usbhost hid-ready line-detected
usbhost stack kernel bridge line-state; PIO host program staged; Linux HCD plus boot-keyboard probes available
usbhost pio-ready yes
usbhost pio-configured yes
usbhost pio-debug index 0 sm-tx 0 sm-rx 1 sm-eop 2 clk-sys-hz 252000000
usbhost pio-clkdiv tx 0x00054000 rx 0x00010000 eop 0x0002a000
usbhost dma present 1 channel 9 packets 15615 last-result 0 ctrl 0x00812013
usbhost rx-attempts 100 rx-errors 100 last-rx-result -110 pid 0x00 len 0
usbhost probe-summary hcd-control setup-arm-ack tx-len=38
usbhost next pio-packet-io first=boot-protocol-keyboard
```

Latest repaired recovery/HCD image, using the `0101` `linux-dirclean` kernel
rebuild plus the current recovery helper/userspace repairs, then exported,
converted, and validated:

```text
flash-image.bin sha256 c897cd796f48e4d256270157e8a689211f4616353bf0bcbb56b5a25acc178ab6
flash-image.uf2 sha256 86bcd9e4571a0fa5bf311df5f7afc6412629898bbbd9bd5dae2dc48f536861dd
Image sha256 04720fbfe11029b89490a1d4b27ca5ce5d172b017e2e223078e1362f99c7806b
adafruit_fruit_jam_rp2350.dtb sha256 1f869eacec12611620a43b590632d39fc9771cdebf2b2c3995f6da7906eeb4ea
bootloader.bin sha256 beae9f2c0f163b761742f4160c6a4250cf358cb9305ece88708a9753e890bcbd
```

This latest source builds on the `0094` HCD timing state with the `0095` retry
sweep, the `0096` upstream-style setup attempt, the `0097` upstream-style status
OUT attempt, the `0098` interrupt/bulk OUT path needed by xpad/XInput-class
drivers, the `0099` broader control/bulk/interrupt transfer support, and
the `0100` no-data control setup sweep, the `0101` interrupt-IN idle NAK
handling, plus recovery repairs:

```text
0093-usb-gadget-acm-add-rp2350-bootsel-touch.patch
  CDC ACM now schedules RP2350 `kernel_restart("bootsel")` when the host sets
  the CDC line coding to 1200 baud. It no longer depends on a separate DTR-low
  control-line transition.

package/fruitjam-airlift/src/airliftctl.c
  AirLift inbound telnet has short input-idle cleanup, stale-session preemption,
  a hard max session age, fast stale socket drop, and periodic inbound recycle
  that is not blocked by an active telnet session. The AirLift telnet path also
  now peeks for an initial `bootsel` payload and calls
  `reboot(..., "bootsel")` directly before allocating a pty or starting
  `/usr/bin/fruitjam-shell`, preserving recovery when no-MMU shell allocation or
  a stale interactive session would be fragile.

package/fruitjam-utils/src/fruitjam-shell.c
  The telnet shell now has a no-fork `bootsel [delay-ms]` built-in that calls
  Linux `reboot(..., "bootsel")` directly. Recovery no longer has to allocate
  and exec `/usr/bin/fruitjamctl` from the telnet shell when memory is tight.

package/fruitjam-utils/src/fruitjam-telnetd.c
  The telnet daemon now peeks briefly for an immediate `bootsel` command before
  it forks the shell. If the recovery helper connects and writes `bootsel 250`
  immediately, the daemon can enter BOOTSEL directly from the listener path.

0095-misc-sweep-fruitjam-usbhost-hcd-ack-arm-gap.patch
  HCD control-read retries now sweep ACK-arm gaps 4, 2, 1, 0, 8, 12, 16, and
  6 bytes. Probe summaries include `gap=` and `tx-len=` so one flashed image can
  test several timing guesses.

0096-misc-use-upstream-style-hcd-setup-probe.patch
  HCD control-read now tries one Pico-PIO-USB/wili8jam-style setup stage before
  the ACK-arm gap sweep. Probe summaries include `mode=upstream` or
  `mode=arm-gap`; if all attempts fail the final summary records that both
  modes and all eight gaps were tried.

0097-misc-use-upstream-style-hcd-status-out.patch
  HCD control-read status OUT now tries the Pico-PIO-USB/wili8jam-style
  prepare/send-OUT/send-DATA1/start-RX/wait-handshake sequence before falling
  back to the local gated ACK-arm status OUT helper.

0098-misc-add-fruitjam-hcd-interrupt-out.patch
  HCD dispatch now accepts interrupt OUT and bulk OUT URBs, sends an OUT token
  followed by the selected DATA0/DATA1 payload, waits for the device handshake,
  maps STALL to `-EPIPE`, retries NAK, and advances the Linux USB OUT toggle on
  ACK. This does not by itself prove gamepad support, but it removes a real
  blocker for Xbox 360/xpad-style drivers that must send interrupt OUT reports
  after enumeration.

0099-misc-expand-fruitjam-hcd-transfer-types.patch
  HCD dispatch now handles control writes with data stages, shared interrupt/bulk
  IN reads, Linux IN data toggles, and bulk-IN dispatch. This is source/build
  coverage for the transfer types keyboard, hub, MSC/CDC-like probes, and xpad
  class drivers may request after endpoint-zero enumeration is fixed.

0100-misc-sweep-fruitjam-hcd-no-data-control.patch
  No-data endpoint-zero requests now use the same upstream-first then arm-gap
  sweep strategy as HCD control reads/writes. This covers SET_ADDRESS,
  SET_CONFIGURATION, SET_PROTOCOL, and SET_IDLE, which are the next enumeration
  requests a hub, keyboard, or gamepad reaches after the first descriptor read.

0101-misc-treat-fruitjam-interrupt-in-nak-as-idle.patch
  Interrupt IN endpoints commonly NAK while idle. wili8jam/TinyUSB leaves those
  transfers live and polls again later; the synchronous Linux HCD now approximates
  that by returning a zero-length successful poll after bounded NAK retries for
  interrupt-IN only. Bulk and control transfers still surface exhausted NAK
  retries as `-EAGAIN`.

scripts/fruitjam-recover-flash.py
  The helper waits longer after accepted recovery triggers and now reports
  silent HTTP/telnet sockets as "sent, no reply/prompt" instead of implying the
  board actually accepted BOOTSEL. If the HTTP BOOTSEL request is sent and the
  socket resets before a reply, the helper now treats that as a possible reboot
  trigger and waits for `picotool` instead of immediately falling through. It
  also tries both `/dev/cu.usbmodem*` and `/dev/tty.usbmodem*` CDC counterparts,
  uses a host `stty` 1200-baud touch before the pyserial 1200-baud path, and
  pauses after failed CDC opens. Telnet recovery now sends the immediate
  `bootsel 250` payload before opening a prompt-aware shell session, so it can
  hit the new AirLift/telnetd no-fork paths first. The prompt-aware telnet path
  remains as a fallback and now really sends its BOOTSEL payload after the
  prompt/busy probe; a source guard rejects the earlier broken shape where
  `sock.sendall(...)` was accidentally unreachable after `return False`. The
  telnet payload tries the new `bootsel 250` built-in first, then falls back to
  `/usr/bin/fruitjamctl bootsel 250`. If all recovery routes fail, the helper
  can send a Bark notification through `FJ_BARK_URL`, `BARK_URL`, or
  `--bark-url`, then keeps polling for a configurable
  `FJ_MANUAL_BOOTSEL_TIMEOUT` / `--manual-bootsel-timeout` window so a late or
  one-time manual BOOTSEL can be flashed without rerunning the helper. CDC
  recovery is now ordered per port: shell payload, host `stty` 1200-baud touch,
  pyserial 1200-baud touch, then the next /dev/cu.* or /dev/tty.* node. On
  macOS, if a pyserial open times out and leaves this process holding the CDC
  node, the helper scans its own fds and closes the lingering descriptor before
  trying the next recovery method.
```

The repaired `0101` UF2 has not yet been flashed. The last hardware-observed
running image was in the old recovery failure state:

```text
picotool info -a
  No accessible RP2040/RP2350 devices in BOOTSEL mode were found.

HTTP recovery
  sent direct HTTP BOOTSEL request to 192.168.1.7:80; no HTTP reply before timeout

telnet recovery
  sent fruitjamctl bootsel over telnet 192.168.1.7:23; no prompt before command
  later prompt-aware telnet reset with `Errno 54`; immediate-write telnet sent
  five BOOTSEL commands in the manual probe but also reset each connection and
  did not enter BOOTSEL. The repaired host helper's latest three immediate
  attempts also reset on this old image before BOOTSEL became visible.

CDC shell recovery
  the repaired helper did send the full `bootsel 250`,
  `/usr/bin/fruitjamctl bootsel 250`, and `fruitjamctl bootsel 250` command
  sequence over /dev/cu.usbmodem101 in one run, but BOOTSEL still did not
  appear. A later run hit a serial open timeout on /dev/cu.usbmodem101, and
  /dev/tty.usbmodem101 then reported EBUSY.

CDC 1200-baud recovery
  the helper now tries 1200-baud recovery port-by-port before moving from the
  /dev/cu.* node to the /dev/tty.* counterpart. A standalone host
  `stty -f /dev/cu.usbmodem101 1200` succeeded after the helper exited, but
  `picotool info -a` still did not see ROM BOOTSEL. In the final helper run,
  /dev/cu.usbmodem101 timed out and /dev/tty.usbmodem101 reported EBUSY.

BOOTSEL watch
  BOOTSEL did not appear after the HTTP, telnet, CDC-shell, or CDC-1200 paths
  and a later 240-second host-side watch after the validated `0098` rebuild
  ended with `No accessible RP2040/RP2350 devices in BOOTSEL mode were found`.

2026-06-18 `0099` helper attempt
  The validated `0099` UF2 was ready at
  `buildroot-output-docker-images/flash-image.uf2` with SHA256
  `95d8a4bd8894777f54b17a27fade34a707dbb71e195eddb59159ff8070f22a38`.
  `scripts/fruitjam-recover-flash.py -v --http-host 192.168.1.7
  --telnet-host 192.168.1.7 --serial-open-timeout 15
  --post-trigger-bootsel-timeout 45 --manual-bootsel-timeout 240` reached the
  old image over HTTP/telnet but both reset before BOOTSEL. It later saw
  `/dev/cu.usbmodem101` and sent the CDC shell BOOTSEL command sequence, then
  tried `stty` and pyserial 1200-baud touch paths; CDC timed out or reported
  busy. The repaired manual/late fallback wait also timed out. A final
  `picotool info -a` still reported no accessible BOOTSEL device, and no
  `/dev/cu.usbmodem*`, `/dev/tty.usbmodem*`, or USB-serial device was visible.

2026-06-18 `0100` source/build step
  Added `0100-misc-sweep-fruitjam-hcd-no-data-control.patch`, validated the
  patch against the built Linux tree, rebuilt with Docker `linux-dirclean`,
  exported and converted the image, and validated the UF2. The built
  `drivers/misc/fruitjam_usbhost.c` contains the new `no-data failed
  modes=upstream,arm-gap` and no-data `status-in-ok` probe summaries. A final
  `picotool info -a` still reported no accessible BOOTSEL device, and no
  `/dev/cu.usbmodem*`, `/dev/tty.usbmodem*`, or USB-serial device was visible,
  so the `0100` UF2 has not been flashed or hardware-tested.

2026-06-18 `0101` source/build step
  Added `0101-misc-treat-fruitjam-interrupt-in-nak-as-idle.patch`, validated the
  patch against the built Linux tree, added source guards, rebuilt with Docker
  `linux-dirclean` plus `fruitjam-airlift-dirclean` and `fruitjam-utils-dirclean`,
  exported and converted the image, and validated the UF2. This build also adds
  the AirLift direct-telnet BOOTSEL handler and changes the recovery helper to
  send the immediate telnet BOOTSEL payload before the prompt-aware fallback.
  Final host checks after the build still reported no accessible BOOTSEL device
  and no `/dev/cu.usbmodem*`, `/dev/tty.usbmodem*`, or USB-serial device, so the
  `0101` UF2 has not been flashed or hardware-tested.

2026-06-18 `0101` recovery retry after host helper fd cleanup
  `scripts/fruitjam-recover-flash.py -v --uf2 buildroot-output-docker-images/flash-image.uf2
  --http-host 192.168.1.7 --telnet-host 192.168.1.7 --serial-open-timeout 15
  --post-trigger-bootsel-timeout 45 --manual-bootsel-timeout 90` still could not
  flash the old running image: HTTP and telnet reset connections, and BOOTSEL did
  not appear after CDC recovery. The new host-side fd cleanup was verified on the
  real failure path: after pyserial timed out opening `/dev/cu.usbmodem101` and
  `/dev/tty.usbmodem101`, the helper logged `closed lingering CDC fd 3` and then
  successfully ran `stty -f ... 1200` on both CDC nodes. Final checks still
  reported no accessible BOOTSEL device, no visible `/dev/cu.usbmodem*` or
  `/dev/tty.usbmodem*`, and no lingering `lsof` holder for the CDC nodes.

2026-06-18 `0101` recovery retry after HTTP reset-after-request host fix
  `scripts/fruitjam-recover-flash.py -v --uf2 buildroot-output-docker-images/flash-image.uf2
  --http-host 192.168.1.7 --telnet-host 192.168.1.7 --serial-open-timeout 15
  --post-trigger-bootsel-timeout 45 --manual-bootsel-timeout 0` reached the old
  running image again. The helper logged
  `HTTP BOOTSEL socket closed after request to 192.168.1.7:80: [Errno 54]` and
  `sent direct HTTP BOOTSEL request to 192.168.1.7:80; socket closed before
  reply`, then correctly waited for ROM BOOTSEL. `picotool` still did not see
  the board. Immediate telnet sent all three BOOTSEL payloads and each socket
  reset. CDC later appeared as `/dev/cu.usbmodem101`; the helper sent the shell
  BOOTSEL command sequence successfully, but the old image still did not enter
  BOOTSEL. The `/dev/cu.usbmodem101` 1200-baud methods timed out and the helper
  closed the lingering fd. On `/dev/tty.usbmodem101`, the helper sent both
  `stty` and pyserial 1200-baud touches, but the final `picotool info -a`
  still reported `No accessible RP2040/RP2350 devices in BOOTSEL mode were
  found`. A separate 180-second host-side BOOTSEL watch also timed out. This
  proves the remaining failure is in the currently running old target image, not
  in a host fd leak or false HTTP-reset classification.

2026-06-18 USB input image validation
  `scripts/validate-fruitjam-image.sh buildroot-output-docker-images` now checks
  the exported kernel `Image`, not only the rootfs and UF2. The current validated
  0101 image contains `fruitjam-usbhost`, `usbhid: USB HID core driver`,
  `hid-generic`, `xpad_irq_in`, `xpad_irq_out`, and the kernel xpad device-table
  string `Xbox 360 Wireless Receiver (XBOX)`. This proves the exported image
  still carries the Linux USB HID and Xbox 360 receiver driver surface required
  after the Fruit Jam HCD gets through hub/downstream enumeration.
```

One BOOTSEL flash is still required to seed the repaired recovery paths. After
that, retest both software BOOTSEL paths before continuing USB HCD work:

```sh
python3 scripts/fruitjam-recover-flash.py -v \
  --uf2 buildroot-output-docker-images/flash-image.uf2 \
  --http-host 192.168.1.7 --telnet-host 192.168.1.7 \
  --serial-open-timeout 15 --post-trigger-bootsel-timeout 45

python3 scripts/fruitjam-recover-flash.py -v \
  --uf2 buildroot-output-docker-images/flash-image.uf2 \
  --skip-http --skip-telnet --skip-cdc-shell --no-flash \
  --serial-open-timeout 15 --post-trigger-bootsel-timeout 45
picotool reboot
```

The preceding PIO0/252 MHz/12 mA image was flashed with `picotool load -fu
buildroot-output-docker-images/flash-image.uf2` and came back as
`/dev/cu.usbmodem101`. The Fruit Jam USB-A ports had a Logitech keyboard
receiver and Xbox 360 gamepad/receiver attached during those hardware tests.
That flashed image did not yet include the Linux USB/HID/input/xpad config or
the `0081` line-read workaround.

Low-level checks that passed after moving USB host to PIO0:

```text
fruitjam-usbhost status
  power on gpio11=1 gpio1(dp)=1 gpio2(dm)=0
  device full-speed-device
  pio-ready yes
  pio-configured yes
  pio-debug index 0 sm-tx 0 sm-rx 1 sm-eop 2 clk-sys-hz 252000000
  dma present 1 channel 9

cat /dev/fruitjam-usbhost
  pio 0
  tx_dma_channel 9
  dp_ctrl 0x00010006
  dm_ctrl 0x00010006
  dp_pad 0x00000075
  dm_pad 0x00000075

fruitjam-usbhost self-rx
  receives a valid internally looped SOF packet
  example last-rx-hex 80a50010

fruitjam-usbhost setup-data-self-rx-drain
  receives a valid internally looped DATA0 setup payload
  example last-rx-hex 80c38006000100000800eb94

berry-run /root/berry/neopixels.be
  still works after moving NeoPixels from PIO0 to PIO2
```

External endpoint-zero checks still failed. The current Linux driver does not
get an ACK or descriptor bytes from the onboard CH334F hub:

```text
fruitjam-usbhost reset-get-device-8-frame-retry 16
  failed with Connection timed out
  last-rx-result -110
  last-rx-pid 0x00
  last-rx-len 0

fruitjam-usbhost reset-get-device-8-combo-skipack
  failed with Connection timed out

fruitjam-usbhost hub-set-address-capture
  failed with Protocol error
  captured only the host SETUP echo
  last-rx-hex 802d0010

fruitjam-usbhost hub-set-address-skipack 25
fruitjam-usbhost hub-set-address-upstream 25
fruitjam-usbhost hub-kbd-init-poll
  all timed out before any downstream keyboard/gamepad work could begin
```

A runtime clock sweep at 120 MHz, 144 MHz, 150 MHz, 240 MHz, 252 MHz, and
264 MHz did not change the failure mode: endpoint zero still timed out with
`last-rx-result -110`, PID `0x00`, and length `0`. The source/DTS/artifact
default is 252 MHz. The live board may still be at the final 264 MHz sweep
setting because macOS hung in `tcsetattr` while opening `/dev/cu.usbmodem101`
for a follow-up retune command.

Debug implication:

The Linux side now matches the wili8jam electrical and TinyUSB/Pico-PIO-USB
configuration values that can be expressed in the current DTS and bridge driver,
and now includes Linux HCD registration plus outbound interrupt/bulk URB support.
The last hardware-tested failure was not HID or XInput yet; it was the first
control transfer to the onboard CH334F hub. The latest recovery-repaired HCD
image adds the `0095` ACK-arm gap sweep, `0096`/`0097` upstream-style control
stages, `0098` interrupt/bulk OUT support, `0099` broader transfer support,
`0100` no-data control retries, and `0101` interrupt-IN idle NAK handling. It is
built and validated but unflashed because the current board is not visible
through BOOTSEL, AirLift HTTP/telnet, or CDC from macOS.

The latest rebuilt rootfs includes the fixed `fruitjam-usbhost.c` status string
(`PIO host program staged`) and was regenerated with the kernel image.

## Earlier Fruit Jam Linux USB Host Probe Status - 2026-06-17

Earlier clean Fruit Jam image under test:

```text
flash-image.bin sha256 bf61f4f9e87500b38c1e96bb9cc283dc78b605be15c2d78eb15d2a13cfd125e7
flash-image.uf2 sha256 0ced3fc6fc58e3ffb9d29f7006c6aeaa3551518c030da481b6d685b96be839b7
bootloader.bin sha256 beae9f2c0f163b761742f4160c6a4250cf358cb9305ece88708a9753e890bcbd
Image sha256 c9683980eec255cd58a087b3846b0189587721c3f47ae75c75cae714790c2b03
```

The image includes the wili8jam-style 252 MHz clock path, 1.30 V bootloader
VREG setup, PIO2 USB host on GPIO1/GPIO2, VBUS power on GPIO11, and USB host
TX DMA channel 9. It also includes the framed retry GET_DESCRIPTOR probe in
`0080-misc-add-fruitjam-usbhost-framed-retry-probe.patch`. CDC came back after
flashing as `/dev/cu.usbmodem101`.

Low-level Fruit Jam USB host checks that passed on this image:

```text
fruitjam-usbhost status
  power on gpio11=1 gpio1(dp)=1 gpio2(dm)=0
  device full-speed-device
  pio-ready yes
  pio-configured yes
  pio-debug index 2 sm-tx 0 sm-rx 1 sm-eop 2 clk-sys-hz 252000000
  dma present 1 channel 9

fruitjam-usbhost tx-test
  TX packet count increments
  tx-errors 0

fruitjam-usbhost self-rx
  receives a valid internally looped SOF packet

fruitjam-usbhost setup-token-self-rx
  receives a valid internally looped SETUP token
  last-rx-hex 802d0010

fruitjam-usbhost setup-data-self-rx-drain
  receives a valid internally looped DATA0 setup payload
  last-rx-hex 80c38006000100000800eb94
```

The external endpoint-zero transaction is still the blocker. The onboard CH334F
hub has not ACKed or returned descriptor bytes with the current driver. These
probes all timed out with no useful device response:

```text
fruitjam-usbhost get-device-8-gated
fruitjam-usbhost reset-get-device-8-gated
fruitjam-usbhost reset-get-device-8-fast
fruitjam-usbhost reset-get-device-8-tight
fruitjam-usbhost reset-get-device-8-stream
fruitjam-usbhost reset-get-device-8-combo-skipack
fruitjam-usbhost reset-get-device-8-gap 2
fruitjam-usbhost reset-get-device-8-gap 1
fruitjam-usbhost reset-get-device-8-gap 4
fruitjam-usbhost reset-get-device-8-gap 8
fruitjam-usbhost reset-get-device-8-gap 16
fruitjam-usbhost hub-set-address-upstream 0
```

A runtime clock-declaration sweep at 126 MHz, 144 MHz, and 252 MHz did not make
the hub ACK the control setup packet. VBUS power cycling also restored the
full-speed line state but did not make endpoint zero respond.

Follow-up probes after the mouse LED blinked:

```text
fruitjam-usbhost status after reset
  power on gpio11=1 gpio1(dp)=1 gpio2(dm)=0
  device full-speed-device

fruitjam-usbhost get-device-8-tight repeated 7 times after one reset
  every attempt timed out
  every attempt reported last-rx-result -110 pid 0x00 len 0

fruitjam-usbhost get-device-8-gated-cpu
  timed out with no endpoint-zero response

fruitjam-usbhost setup-data-self-rx-cpu
fruitjam-usbhost self-rx-cpu
  CPU-fed full-speed TX produced corrupt self-RX bytes after the endpoint-zero
  CPU probe. Keep using DMA-fed TX for real full-speed host traffic.

fruitjam-usbhost reset-get-device-8-frame-retry 8
  failed with Connection timed out
  TX path still had tx-errors 0
  packets 63, DMA packets 71
  last-rx-result -110 pid 0x00 len 0

fruitjam-usbhost get-device-8-frame-retry 16
  failed with Connection timed out
  TX path still had tx-errors 0
  packets 95, DMA packets 119
  last-rx-result -110 pid 0x00 len 0
```

User observation during this probe set:

```text
The USB mouse LED blinked when attached to Fruit Jam.
```

Debug implication:

The LED blink is encouraging evidence that power/reset/host activity is
reaching the USB-A path, but it is not enumeration. The current failure is
lower than HID: the first target remains the onboard CH334F hub on endpoint
zero, and it still gives no accepted control-transfer response. Internal
self-RX proves that the Linux driver can encode/decode its own packets, but it
does not prove the physical waveform is accepted by the hub. The next useful
engineering step is to compare or port more of Pico-PIO-USB's host transaction
engine and frame scheduling from wili8jam, or instrument the physical bus.

## Mac Topology During Captures

The first two HID targets were seen behind the Mac's external GenesysLogic USB2
hub:

```text
AppleT8112USBXHCI@01000000
  USB3.1 Hub, GenesysLogic, VID:PID 05e3:0626
  USB2.1 Hub, GenesysLogic, VID:PID 05e3:0610
    HP 125 USB Optical Mouse
    USB Receiver
```

The second capture, with the Logitech optical mouse and Apple USB keyboard,
showed this topology:

```text
AppleT8112USBXHCI@01000000
  USB3.1 Hub, GenesysLogic, VID:PID 05e3:0626
  USB2.1 Hub, GenesysLogic, VID:PID 05e3:0610
    Keyboard Hub, Apple, VID:PID 05ac:1006
      Apple Keyboard, VID:PID 05ac:0221
    Optical USB Mouse, Logitech, VID:PID 046d:c016
```

The third capture, with an FTDI serial adapter and old Pro Media webcam, showed
this topology:

```text
AppleT8112USBXHCI@01000000
  USB3.1 Hub, GenesysLogic, VID:PID 05e3:0626
  USB2.1 Hub, GenesysLogic, VID:PID 05e3:0610
    USB Storage, GenesysLogic, VID:PID 05e3:0751
    PC Camera, Vimicro Corp., VID:PID 0ac8:301b
    PropPlug, Parallax Inc, VID:PID 0403:6015
```

The fourth capture, with a Plantronics headset and Xbox 360 controller
receiver, showed this topology:

```text
AppleT8112USBXHCI@01000000
  USB3.1 Hub, GenesysLogic, VID:PID 05e3:0626
  USB2.1 Hub, GenesysLogic, VID:PID 05e3:0610
    USB Storage, GenesysLogic, VID:PID 05e3:0751
    Xbox 360 controller receiver, no USB strings, VID:PID 045e:0291
    Plantronics .Audio 628 USB, Plantronics, VID:PID 047f:c012
```

This matters because the Fruit Jam USB-A ports are also downstream of an
onboard hub. A working Fruit Jam path must enumerate the hub first, reset the
selected downstream port, then enumerate the attached USB device behind that
port.

## Fruit Jam USB Host Schematic Notes

Schematic snippets supplied during debugging confirm this Fruit Jam host path:

```text
RP2350 GPIO1/GPIO2 USBH_D+/USBH_D-
  -> CH334F upstream D+U/D-U
  -> CH334F downstream D+/D-1 and D+/D-2
  -> two external USB-A ports
```

The CH334F hub uses a 12 MHz crystal. That supports the current driver default
of a 144 MHz `clk_sys` value derived from the board DTS rather than the older
150 MHz assumption.

USB-A VBUS is switched by the high-side DMP3098L path controlled through the
`USBH_PWR` signal. The gate is pulled up to 5 V, and the BSS138 pulldown turns
the high-side switch on when `USBH_PWR` is driven high. This matches the Linux
board wiring and tools:

```text
raspberrypi,power-gpio = <11>
fruitjamctl usb-power on -> GPIO11 high
fruitjam-services startup -> GPIO11 high
```

Debug implications:

1. If a USB mouse LED stays dark, first verify 5 V VBUS at the USB-A connector
   or with `fruitjam-usbhost status` after power is enabled.
2. If `fruitjam-usbhost status` shows `power 1` and `dp 1 dm 0`, the CH334F is
   at least powered enough to present a full-speed upstream pull-up.
3. The first software enumeration target is the CH334F root hub, not the
   Logitech receiver, Logitech mouse, HP mouse, or Apple keyboard.
4. HID work only starts after root-hub address/configuration and downstream
   port reset/power succeed.

## HP 125 USB Optical Mouse

Photo label:

```text
HP Inc. Wired Mouse
HP 125 USB Optical Mouse
Model: TPA-L001M
Rating: 5.0 V, 100 mA
ASSY P/N: P10272-001
SPS P/N: P10666-001
```

Device descriptor from macOS/PyUSB:

```text
Product: HP 125 USB Optical Mouse
Manufacturer: PixArt
VID:PID: 03f0:584a
idVendor decimal: 1008
idProduct decimal: 22602
bcdUSB: 0x0200
bcdDevice: 0x0100
bDeviceClass: 0x00, class specified at interface
bDeviceSubClass: 0x00
bDeviceProtocol: 0x00
bMaxPacketSize0: 8
bNumConfigurations: 1
Configuration: 1
bmAttributes: 0xa0, bus powered, remote wakeup
bMaxPower: 100 mA
UsbLinkSpeed: 1500000, low-speed
```

Interface and endpoint:

```text
Interface 0
  bInterfaceClass: 0x03, HID
  bInterfaceSubClass: 0x01, boot interface
  bInterfaceProtocol: 0x02, mouse
  bNumEndpoints: 1

Endpoint 0x81
  Direction: IN
  Type: interrupt
  wMaxPacketSize: 8
  bInterval: 10
```

HID facts:

```text
hidutil UsagePage: 1, Generic Desktop
hidutil Usage: 2, Mouse
BootProtocol: 2
PrimaryUsagePage: 1
PrimaryUsage: 2
MaxInputReportSize: 8
MaxOutputReportSize: 0
MaxFeatureReportSize: 0
ReportInterval: 8000
```

Report descriptor:

```text
05010902a1010901a100050919012903150025017501950381027505950181030600ff0940950275081581257f8102050109381581257f7508950181060930093116018026ff7f751095028106c0c0
```

Debug implication:

The HP mouse is a plain low-power HID mouse, but it is low-speed. It is not the
best first device for root-port timing tests. Behind the Fruit Jam onboard hub,
it also requires hub low-speed downstream handling before HID polling can work.

## Logitech USB Receiver

Device descriptor from macOS/PyUSB:

```text
Product: USB Receiver
Manufacturer: Logitech
VID:PID: 046d:c534
idVendor decimal: 1133
idProduct decimal: 50484
bcdUSB: 0x0200
bcdDevice: 0x5200
bDeviceClass: 0x00, class specified at interface
bDeviceSubClass: 0x00
bDeviceProtocol: 0x00
bMaxPacketSize0: 64
bNumConfigurations: 1
Configuration: 1
iConfiguration: RQR52.00_B0008
bmAttributes: 0xa0, bus powered, remote wakeup
bMaxPower: 98 mA
UsbLinkSpeed: 12000000, full-speed
```

Interface 0, keyboard:

```text
Interface 0
  bInterfaceClass: 0x03, HID
  bInterfaceSubClass: 0x01, boot interface
  bInterfaceProtocol: 0x01, keyboard
  bNumEndpoints: 1

Endpoint 0x81
  Direction: IN
  Type: interrupt
  wMaxPacketSize: 64
  bInterval: 2
```

Interface 1, mouse:

```text
Interface 1
  bInterfaceClass: 0x03, HID
  bInterfaceSubClass: 0x01, boot interface
  bInterfaceProtocol: 0x02, mouse
  bNumEndpoints: 1

Endpoint 0x82
  Direction: IN
  Type: interrupt
  wMaxPacketSize: 64
  bInterval: 2
```

HID facts from `hidutil list`:

```text
046d:c534 UsagePage 1 Usage 6 Product "USB Receiver"
046d:c534 UsagePage 1 Usage 2 Product "USB Receiver"
```

The receiver exposes both boot keyboard and boot mouse HID functions. The
keyboard being turned off should not prevent USB enumeration of the receiver
itself. It only affects whether keyboard input reports change after polling.

Keyboard HID service:

```text
BootProtocol: 1
PrimaryUsagePage: 1
PrimaryUsage: 6
MaxInputReportSize: 8
MaxOutputReportSize: 1
MaxFeatureReportSize: 0
ReportInterval: 2000
```

Keyboard report descriptor:

```text
05010906a1019508750115002501050719e029e7810281039505050819012905910295017503910395067508150026ff00050719002aff008100c0
```

Mouse HID service:

```text
BootProtocol: 2
PrimaryUsagePage: 1
PrimaryUsage: 2
MaxInputReportSize: 20
MaxOutputReportSize: 20
MaxFeatureReportSize: 1
ReportInterval: 2000
```

Mouse report descriptor:

```text
05010902a10185020901a100951075011500250105091901291081029502750c1601f826ff070501093009318106950175081581257f093881069501050c0a38028106c0c0050c0901a101850395027510150126ff0219012aff028100c005010980a101850495017502150125030982098109838100750115002501099b810675058103c00600ff0901a101851095067508150026ff000901810009019100c00600ff0902a101851195137508150026ff000902810009029100c0
```

Debug implication:

The Logitech receiver is the best first Fruit Jam target because it is
full-speed and exposes a simple boot-keyboard interface at interface 0,
interrupt IN endpoint 1. After hub support, the expected narrow test target is:

```text
hub address: 1
receiver address: 2
configuration: 1
keyboard interface: 0
keyboard endpoint: 1, address 0x81
mouse interface: 1
mouse endpoint: 2, address 0x82
```

## Logitech Optical USB Mouse

Device descriptor from macOS/PyUSB:

```text
Product: Optical USB Mouse
Manufacturer: Logitech
VID:PID: 046d:c016
idVendor decimal: 1133
idProduct decimal: 49174
bcdUSB: 0x0200
bcdDevice: 0x0340
bDeviceClass: 0x00, class specified at interface
bDeviceSubClass: 0x00
bDeviceProtocol: 0x00
bMaxPacketSize0: 8
bNumConfigurations: 1
Configuration: 1
bmAttributes: 0xa0, bus powered, remote wakeup
bMaxPower: 100 mA
UsbLinkSpeed: 1500000, low-speed
USB address on Mac during capture: 6
LocationID on Mac during capture: 18022400
```

Interface and endpoint:

```text
Interface 0
  bInterfaceClass: 0x03, HID
  bInterfaceSubClass: 0x01, boot interface
  bInterfaceProtocol: 0x02, mouse
  bNumEndpoints: 1
  HID descriptor extra: 092110010001223400

Endpoint 0x81
  Direction: IN
  Type: interrupt
  wMaxPacketSize: 4
  bInterval: 10
```

HID facts:

```text
hidutil UsagePage: 1, Generic Desktop
hidutil Usage: 2, Mouse
BootProtocol: 2
PrimaryUsagePage: 1
PrimaryUsage: 2
MaxInputReportSize: 4
MaxOutputReportSize: 0
MaxFeatureReportSize: 0
ReportInterval: 8000
```

Report descriptor:

```text
05010902a1010901a1000509190129031500250175019503810275059501810105010930093109381581257f750895038106c0c0
```

Debug implication:

This is a simple Logitech low-speed boot mouse. It should be useful after the
Fruit Jam host path can enumerate the onboard hub and handle low-speed
downstream traffic. It is not as good as the Logitech receiver for the first
full-speed test because it adds low-speed handling immediately.

## Apple USB Keyboard

The Apple USB keyboard is a compound device. The cable first enumerates as an
Apple USB hub, then the keyboard function enumerates as a separate low-speed
device behind that hub.

### Apple Keyboard Hub

Device descriptor from macOS/PyUSB:

```text
Product: Keyboard Hub
Manufacturer: Apple, Inc.
VID:PID: 05ac:1006
idVendor decimal: 1452
idProduct decimal: 4102
bcdUSB: 0x0200
bcdDevice: 0x9415
bDeviceClass: 0x09, hub
bDeviceSubClass: 0x00
bDeviceProtocol: 0x01, single TT
bMaxPacketSize0: 64
bNumConfigurations: 1
Configuration: 1
bmAttributes: 0xa0, bus powered, remote wakeup
bMaxPower: 300 mA
UsbLinkSpeed on Mac: 480000000, high-speed
USB address on Mac during capture: 4
LocationID on Mac during capture: 17956864
Serial string: 000000000000
```

Interface and endpoint:

```text
Interface 0
  bInterfaceClass: 0x09, hub
  bInterfaceSubClass: 0x00
  bInterfaceProtocol: 0x00
  bNumEndpoints: 1

Endpoint 0x81
  Direction: IN
  Type: interrupt
  wMaxPacketSize: 1
  bInterval: 12
```

Debug implication:

This keyboard adds another hub between the Fruit Jam onboard hub and the actual
keyboard interface. It is a useful future compatibility target, but it is not a
minimal first HID target.

### Apple Keyboard Device

Device descriptor from macOS/PyUSB:

```text
Product: Apple Keyboard
Manufacturer: Apple, Inc
VID:PID: 05ac:0221
idVendor decimal: 1452
idProduct decimal: 545
bcdUSB: 0x0200
bcdDevice: 0x0069
bDeviceClass: 0x00, class specified at interface
bDeviceSubClass: 0x00
bDeviceProtocol: 0x00
bMaxPacketSize0: 8
bNumConfigurations: 1
Configuration: 1
bmAttributes: 0xa0, bus powered, remote wakeup
bMaxPower: 20 mA
UsbLinkSpeed: 1500000, low-speed
USB address on Mac during capture: 5
LocationID on Mac during capture: 17965056
```

Interface 0, boot keyboard:

```text
Interface 0
  bInterfaceClass: 0x03, HID
  bInterfaceSubClass: 0x01, boot interface
  bInterfaceProtocol: 0x01, keyboard
  bNumEndpoints: 1
  HID descriptor extra: 092111010d01224b00

Endpoint 0x81
  Direction: IN
  Type: interrupt
  wMaxPacketSize: 8
  bInterval: 10
```

Interface 1, consumer control:

```text
Interface 1
  bInterfaceClass: 0x03, HID
  bInterfaceSubClass: 0x00
  bInterfaceProtocol: 0x00
  bNumEndpoints: 1
  HID descriptor extra: 092111010001222f00

Endpoint 0x82
  Direction: IN
  Type: interrupt
  wMaxPacketSize: 1
  bInterval: 10
```

Boot-keyboard HID service:

```text
BootProtocol: 1
PrimaryUsagePage: 1
PrimaryUsage: 6
MaxInputReportSize: 8
MaxOutputReportSize: 1
MaxFeatureReportSize: 0
ReportInterval: 8000
```

Boot-keyboard report descriptor:

```text
05010906a101050719e029e715002501750195088102950175088101050819012905950575019102950175039101050719002aff0095057508150026ff00810005ff0903750895018102c0
```

Consumer-control HID service:

```text
BootProtocol: 0
PrimaryUsagePage: 12
PrimaryUsage: 1
MaxInputReportSize: 1
MaxOutputReportSize: 0
MaxFeatureReportSize: 0
ReportInterval: 8000
```

Consumer-control report descriptor:

```text
050c0901a101050c750195011500250109cd810609b5810209b6810209b8810609e2810609ea810209e981028101c0
```

Debug implication:

The actual Apple keyboard interface is a normal low-speed boot keyboard once
its own hub is configured. On Fruit Jam, the chain would be onboard CH334F hub,
then Apple keyboard hub, then low-speed keyboard device.

## Parallax PropPlug FTDI USB Serial Adapter

The user described this as an FTDI chip. macOS/PyUSB identified it as a
Parallax PropPlug using an FTDI vendor ID.

Device descriptor from macOS/PyUSB:

```text
Product: PropPlug
Manufacturer: Parallax Inc
VID:PID: 0403:6015
idVendor decimal: 1027
idProduct decimal: 24597
bcdUSB: 0x0200
bcdDevice: 0x1000
bDeviceClass: 0x00, class specified at interface
bDeviceSubClass: 0x00
bDeviceProtocol: 0x00
bMaxPacketSize0: 8
bNumConfigurations: 1
Configuration: 1
bmAttributes: 0xa0, bus powered, remote wakeup
bMaxPower: 90 mA
UsbLinkSpeed: 12000000, full-speed
USB address on Mac during capture: 4
LocationID on Mac during capture: 17956864
Serial string: P97cvdxp
```

Interface and endpoints:

```text
Interface 0
  bInterfaceClass: 0xff, vendor-specific
  bInterfaceSubClass: 0xff
  bInterfaceProtocol: 0xff
  bNumEndpoints: 2

Endpoint 0x81
  Direction: IN
  Type: bulk
  wMaxPacketSize: 64
  bInterval: 0

Endpoint 0x02
  Direction: OUT
  Type: bulk
  wMaxPacketSize: 64
  bInterval: 0
```

macOS binding:

```text
Driver: AppleUSBFTDI
Callout device: /dev/cu.usbserial-P97cvdxp
Dial-in device: /dev/tty.usbserial-P97cvdxp
```

Debug implication:

This is a full-speed USB serial target with bulk IN and bulk OUT endpoints. It
is useful after hub enumeration and basic control/bulk transfers work, but it
is not a HID device. Talking to it as a serial adapter also needs the FTDI
vendor-specific control setup for baud rate, line state, and related serial
configuration.

## Pro Media Webcam / Vimicro PC Camera

The user described this as an old Pro Media webcam. The USB descriptors identify
it as a Vimicro Corp. PC Camera.

Device descriptor from macOS/PyUSB:

```text
Product: PC Camera
Manufacturer: Vimicro Corp.
VID:PID: 0ac8:301b
idVendor decimal: 2760
idProduct decimal: 12315
bcdUSB: 0x0110
bcdDevice: 0x0100
bDeviceClass: 0xff, vendor-specific
bDeviceSubClass: 0x00
bDeviceProtocol: 0x00
bMaxPacketSize0: 8
bNumConfigurations: 1
Configuration: 1
bmAttributes: 0x80, bus powered
bMaxPower: 160 mA
UsbLinkSpeed: 12000000, full-speed
USB address on Mac during capture: 5
LocationID on Mac during capture: 18022400
```

Interface and endpoints:

```text
Interface 0, alternate setting 0
  bInterfaceClass: 0xff, vendor-specific
  bInterfaceSubClass: 0xff
  bInterfaceProtocol: 0xff
  bNumEndpoints: 2

Endpoint 0x81
  Direction: IN
  Type: isochronous
  wMaxPacketSize: 0
  bInterval: 1

Endpoint 0x82
  Direction: IN
  Type: interrupt
  wMaxPacketSize: 8
  bInterval: 10
```

Alternate settings:

```text
alt 0: isochronous IN 0x81 max packet 0, interrupt IN 0x82 max packet 8
alt 1: isochronous IN 0x81 max packet 128, interrupt IN 0x82 max packet 8
alt 2: isochronous IN 0x81 max packet 192, interrupt IN 0x82 max packet 8
alt 3: isochronous IN 0x81 max packet 256, interrupt IN 0x82 max packet 8
alt 4: isochronous IN 0x81 max packet 384, interrupt IN 0x82 max packet 8
alt 5: isochronous IN 0x81 max packet 512, interrupt IN 0x82 max packet 8
alt 6: isochronous IN 0x81 max packet 768, interrupt IN 0x82 max packet 8
alt 7: isochronous IN 0x81 max packet 896, interrupt IN 0x82 max packet 8
```

macOS camera check:

```text
system_profiler SPCameraDataType listed the built-in FaceTime HD camera and
Continuity Camera iPhone only. It did not list this Vimicro USB camera as a
usable camera device during the capture.
```

Debug implication:

This is not a UVC-class webcam from the descriptor data. It is a full-speed,
vendor-specific camera that uses isochronous IN video transfers plus an
interrupt IN endpoint. It is a poor first Fruit Jam USB-host target because it
requires hub enumeration, vendor-specific camera knowledge, alternate-interface
selection, and isochronous transfer support.

## Plantronics .Audio 628 USB Headset

Device descriptor from macOS/PyUSB:

```text
Product: Plantronics .Audio 628 USB
Manufacturer: Plantronics
VID:PID: 047f:c012
idVendor decimal: 1151
idProduct decimal: 49170
bcdUSB: 0x0200
bcdDevice: 0x0243
bDeviceClass: 0x00, class specified at interface
bDeviceSubClass: 0x00
bDeviceProtocol: 0x00
bMaxPacketSize0: 64
bNumConfigurations: 1
Configuration: 1
bmAttributes: 0xa0, bus powered, remote wakeup
bMaxPower: 120 mA
UsbLinkSpeed: 12000000, full-speed
USB address on Mac during capture: 4
LocationID on Mac during capture: 18022400
```

Interface summary:

```text
Interface 0, alternate setting 0
  bInterfaceClass: 0x01, audio
  bInterfaceSubClass: 0x01, audio control
  bInterfaceProtocol: 0x00
  bNumEndpoints: 0

Interface 1, alternate setting 0
  bInterfaceClass: 0x01, audio
  bInterfaceSubClass: 0x02, audio streaming
  bInterfaceProtocol: 0x00
  bNumEndpoints: 0

Interface 1, alternate setting 1
  bInterfaceClass: 0x01, audio
  bInterfaceSubClass: 0x02, audio streaming
  bInterfaceProtocol: 0x00
  bNumEndpoints: 2

Endpoint 0x01
  Direction: OUT
  Type: isochronous
  wMaxPacketSize: 200
  bInterval: 1

Endpoint 0x83
  Direction: IN
  Type: isochronous
  wMaxPacketSize: 3
  bInterval: 1

Interface 2, alternate setting 0
  bInterfaceClass: 0x01, audio
  bInterfaceSubClass: 0x02, audio streaming
  bInterfaceProtocol: 0x00
  bNumEndpoints: 0

Interface 2, alternate setting 1
  bInterfaceClass: 0x01, audio
  bInterfaceSubClass: 0x02, audio streaming
  bInterfaceProtocol: 0x00
  bNumEndpoints: 1

Endpoint 0x81
  Direction: IN
  Type: isochronous
  wMaxPacketSize: 200
  bInterval: 1

Interface 3, alternate setting 0
  bInterfaceClass: 0x03, HID
  bInterfaceSubClass: 0x00
  bInterfaceProtocol: 0x00
  bNumEndpoints: 1
  HID descriptor extra: 092100010001228300

Endpoint 0x84
  Direction: IN
  Type: interrupt
  wMaxPacketSize: 4
  bInterval: 10
```

macOS audio binding:

```text
CoreAudio name: Plantronics .Audio 628 USB
Manufacturer: Plantronics
Transport: USB
Input channels: 2
Output channels: 2
Current sample rate: 44100
Input source: Default
Output source: Default
```

HID facts from `hidutil list` and `AppleUserUSBHostHIDDevice`:

```text
047f:c012 UsagePage 12 Usage 1 Product "Plantronics .Audio 628 USB"
BootProtocol: 0
PrimaryUsagePage: 12, Consumer
PrimaryUsage: 1, Consumer Control
MaxInputReportSize: 3
MaxOutputReportSize: 6
MaxFeatureReportSize: 1
ReportInterval: 8000
```

HID report descriptor:

```text
050c0901a10185080900150026ff007508950591028509090095028102c006a0ff0905a10185021500250109b109b2750195028106090009009502810709b59501812209b79501810609ab95018122099e95018103950881030508090009009502910206a0ff098d098f09b59503912209dc95019106090095019102099e95019122c0
```

Debug implication:

This is a full-speed USB audio headset with a consumer-control HID interface.
The HID controls may be useful after hub enumeration and interrupt-IN polling
work, but the actual audio path needs USB audio class control handling,
alternate-interface selection, and isochronous transfers.

## Xbox 360 Controller Receiver

The user described this as an Xbox 360 controller receiver. The USB descriptor
contains no manufacturer, product, or serial strings, but macOS/PyUSB captured
the Microsoft VID and product ID.

Device descriptor from macOS/PyUSB:

```text
Product: no USB product string
Manufacturer: no USB manufacturer string
VID:PID: 045e:0291
idVendor decimal: 1118
idProduct decimal: 657
bcdUSB: 0x0200
bcdDevice: 0x0107
bDeviceClass: 0xff, vendor-specific
bDeviceSubClass: 0xff
bDeviceProtocol: 0xff
bMaxPacketSize0: 8
bNumConfigurations: 1
Configuration: 1
bmAttributes: 0xa0, bus powered, remote wakeup
bMaxPower: 260 mA
UsbLinkSpeed: 12000000, full-speed
USB address on Mac during capture: 5
LocationID on Mac during capture: 17956864
```

Interface and endpoint summary:

```text
Interface 0
  class/subclass/protocol: 0xff/0x5d/0x81
  extra: 1422000113811d001701020813010c000c010208
  endpoint 0x81 IN interrupt, max packet 32, interval 1
  endpoint 0x01 OUT interrupt, max packet 32, interval 8

Interface 1
  class/subclass/protocol: 0xff/0x5d/0x82
  extra: 0c2200010182004001022000
  endpoint 0x82 IN interrupt, max packet 32, interval 2
  endpoint 0x02 OUT interrupt, max packet 32, interval 4

Interface 2
  class/subclass/protocol: 0xff/0x5d/0x81
  extra: 1422000113831d001701020813030c000c010208
  endpoint 0x83 IN interrupt, max packet 32, interval 1
  endpoint 0x03 OUT interrupt, max packet 32, interval 8

Interface 3
  class/subclass/protocol: 0xff/0x5d/0x82
  extra: 0c2200010184004001042000
  endpoint 0x84 IN interrupt, max packet 32, interval 2
  endpoint 0x04 OUT interrupt, max packet 32, interval 4

Interface 4
  class/subclass/protocol: 0xff/0x5d/0x81
  extra: 1422000113851d001701020813050c000c010208
  endpoint 0x85 IN interrupt, max packet 32, interval 1
  endpoint 0x05 OUT interrupt, max packet 32, interval 8

Interface 5
  class/subclass/protocol: 0xff/0x5d/0x82
  extra: 0c2200010186004001062000
  endpoint 0x86 IN interrupt, max packet 32, interval 2
  endpoint 0x06 OUT interrupt, max packet 32, interval 4

Interface 6
  class/subclass/protocol: 0xff/0x5d/0x81
  extra: 1422000113871d001701020813070c000c010208
  endpoint 0x87 IN interrupt, max packet 32, interval 1
  endpoint 0x07 OUT interrupt, max packet 32, interval 8

Interface 7
  class/subclass/protocol: 0xff/0x5d/0x82
  extra: 0c2200010188004001082000
  endpoint 0x88 IN interrupt, max packet 32, interval 2
  endpoint 0x08 OUT interrupt, max packet 32, interval 4
```

macOS HID check:

```text
hidutil list did not show a HID service for 045e:0291 during this capture.
```

Debug implication:

This is a full-speed, vendor-specific Xbox receiver with multiple interrupt
IN/OUT endpoint pairs. It may be useful later for generic interrupt transfer
testing, but it is not HID and is not a minimal first target. It also asks for
260 mA, higher than the simple mice, keyboard receiver, and FTDI adapter.

## Current Fruit Jam Debugging Conclusion

The device data supports the current diagnosis:

1. Fruit Jam USB-A must enumerate the onboard hub before the mouse or receiver
   can be reached.
2. The Logitech receiver should be used first because it is full-speed and has
   boot keyboard HID on interface 0.
3. The Apple keyboard is not a minimal direct keyboard target; it adds its own
   hub before the low-speed keyboard device.
4. The HP and Logitech optical mice are valid and low power, but low-speed
   handling adds another variable. Use them after the hub and full-speed
   Logitech receiver path work.
5. The FTDI PropPlug is a good later full-speed bulk/control target, but it is
   not HID and needs FTDI-specific serial setup before useful data exchange.
6. The Vimicro webcam is not a good early target; it is vendor-specific and uses
   isochronous video endpoints.
7. The Plantronics headset is a good future audio-class target, but the real
   audio stream needs isochronous support; its consumer-control HID interface
   is the simplest part of the device.
8. The Xbox 360 receiver is full-speed but vendor-specific, multi-interface,
   interrupt-heavy, and asks for 260 mA. It is not a good first target.
9. The mouse LED staying off on Fruit Jam can still be consistent with the hub
   or downstream-port path not being enabled/reset/enumerated yet. It is not
   evidence that the mouse requires too much current.

## Working `wili8jam` USB Host Reference

Source:

```text
https://github.com/freewili/wili8jam/tree/main/usb-host
```

The `wili8jam` firmware is a useful known-good Fruit Jam reference because it
uses the same board USB-A path successfully with keyboard, mouse, and gamepad
input.

Observed setup from `src/main.cpp` and `usb-host/tusb_config.h`:

1. It enables USB-A VBUS with GPIO11 driven high.
2. It configures Pico-PIO-USB host port 1 with D+ on GPIO1 and D- on GPIO2.
3. It uses TinyUSB host on root hub port 1 with `CFG_TUH_RPI_PIO_USB=1`.
4. It enables hub support with `CFG_TUH_HUB=1`.
5. It allows four HID instances and four devices with `CFG_TUH_HID=4` and
   `CFG_TUH_DEVICE_MAX=4`.
6. It raises core voltage to 1.30 V and sets `clk_sys` to 252 MHz before
   starting the USB host path.

Low-level Pico-PIO-USB comparison:

1. The TX encoder matches the Linux driver algorithm: sync/PID/payload/CRC,
   bit stuffing, SE0, COMP, and K trailer.
2. The RX and EOP PIO instruction words match the generated Pico-PIO-USB
   full-speed programs.
3. The RX state machine runs at max PIO speed (`clkdiv=1`) in both stacks.
4. TinyUSB sends SETUP token and DATA0 as separate bus packets, then starts RX
   and waits for ACK. It does not merge SETUP token and DATA0 into one USB
   packet.
5. For Linux, the most important part to emulate is the timing of when RX/EOP
   are cleared and armed around the DATA0 EOP, because CPU-side turnaround is
   much slower than the bare-metal TinyUSB path.

Debug implication:

The working repo confirms the Fruit Jam pinout and VBUS polarity are correct.
It also confirms that the onboard hub path is expected and that HID support
should begin with hub/control endpoint zero enumeration, not with HID report
parsing. The unresolved Linux failure is still below HID: our host can drive
PIO packets and decode some self-RX traffic, but real devices have not answered
control endpoint zero yet.

The 252 MHz detail is a clue, not a ready Linux fix. A temporary 252 MHz Linux
image booted only partially: network ports accepted connections, but telnet,
HTTP, FTP, and USB CDC did not provide usable command paths. Keep the default
144 MHz runtime image as the safe recovery baseline until a narrower timing fix
is proven.

## 2026-06-18 Recovery and XInput Rebuild

A replacement image was built after finding a software BOOTSEL reset bug in the
Linux RP2350 restart patch. The old patch programmed `PSM_WDSEL` as all bits
except `PROC_COLD`; Pico SDK watchdog reset code resets everything except ROSC
and XOSC. That mismatch can explain the observed old-image behavior where HTTP
or telnet accepts a BOOTSEL request, drops services, and then never appears as
RP2350 ROM BOOTSEL. The patch now uses:

```text
RP2350_PSM_WDSEL_BITS &
       ~(RP2350_PSM_WDSEL_ROSC | RP2350_PSM_WDSEL_XOSC)
```

The validation script now guards this exact mask and fails if
`RP2350_PSM_WDSEL_PROC_COLD` is reintroduced in the BOOTSEL restart patch.

The same replacement image includes the wili8jam-inspired USB-host TX-buffer
fix for HID/XInput OUT traffic. `FJ_USBHOST_TX_ENCODED_MAX` is now `192u`,
large enough for the bit-stuffed encoded form of a full-speed 64-byte
HID/XInput payload. This matters for HID feature/output reports and for Xbox
360 receiver initialization traffic.

Validated locally:

```text
./scripts/validate-fruitjam-examples.sh
./scripts/validate-fruitjam-image.sh buildroot-output-docker-images
```

Built kernel source checks:

```text
arch/riscv/kernel/reset.c:
  RP2350_PSM_WDSEL_ROSC / RP2350_PSM_WDSEL_XOSC mask present

drivers/misc/fruitjam_usbhost.c:
  #define FJ_USBHOST_TX_ENCODED_MAX 192u
```

Replacement artifacts:

```text
buildroot-output-docker-images/flash-image.uf2
sha256 727cbdcfe6a506dc08c8c4d66cb5adfecf0db50452d84e7ca0817e57b9b1f219

buildroot-output-docker-images/flash-image.bin
sha256 0104c1b6f7b5caf5411babfe577cac58625beb34917bc7a51b6bdc7de1ba6fc4

buildroot-output-docker-images/Image
sha256 1f1259a0d8dbad36e5da178aaa8f3e59be570c273cbc92921a264df4278e32f7

buildroot-output-docker-images/bootloader.bin
sha256 beae9f2c0f163b761742f4160c6a4250cf358cb9305ece88708a9753e890bcbd
```

Hardware status at this checkpoint:

```text
picotool info -a
No accessible RP2040/RP2350 devices in BOOTSEL mode were found.
```

Two auto-flash watch windows were run after the rebuild. Neither saw the RP2350
ROM device, so the replacement UF2 was not flashed and USB host keyboard/gamepad
behavior has not been re-tested on hardware yet. The old running image still
shows the broken recovery mode: HTTP and telnet ports accept connections, but
BOOTSEL requests reset or wedge the connection and never produce a visible ROM
device; CDC open and 1200-baud touch can time out on `/dev/cu.usbmodem101`.

The post-flash USB host smoke command is now:

```sh
python3 scripts/usbhost-hcd-smoke.py \
  --transport cdc \
  --port /dev/cu.usbmodem101 \
  --serial-open-timeout 10 \
  -v
```

or, if telnet is the working shell on the repaired image:

```sh
python3 scripts/usbhost-hcd-smoke.py \
  --transport telnet \
  --telnet-host 192.168.1.7 \
  -v
```

The smoke script now checks the two devices that are physically plugged into
Fruit Jam as separate requirements:

```text
logitech receiver usb  -> Logitech or 046d:* USB device enumerated
hid keyboard input     -> Linux input registry has a keyboard event handler
xbox receiver usb      -> Xbox/xpad USB device enumerated
xpad gamepad input     -> Linux input registry has xpad event/js handler
```

`scripts/usbhost-hcd-smoke.py --self-test` exercises those checks with fake
Logitech and Xbox registry data, and `./scripts/validate-fruitjam-examples.sh`
now guards that the checks remain present and active.

## 2026-06-18 Recovery Watchdog Rebuild

The prior replacement image still required a one-time manual BOOTSEL flash
because the old running image could accept recovery requests and then fail to
appear as ROM BOOTSEL. The source now has another recovery layer:

* The Fruit Jam defconfig builds the bootloader with
  `BR2_PACKAGE_PICO2_BOOTLOADER_RESCUE_WATCHDOG_MS=15000`.
* The bootloader arms that watchdog immediately before jumping to Linux. If
  Linux never gets far enough to clear it, the next bootloader pass enters
  ROM BOOTSEL automatically.
* The Linux RP2350 restart patch clears only that bootloader rescue watchdog
  from an `early_initcall`, so healthy boots do not trip the rescue timer.
* The bootloader also recognizes a BOOTSEL watchdog vector that reaches the
  flash bootloader. If the ROM BOOTSEL vector ever falls through, the
  bootloader calls the ROM USB loader directly instead of continuing Linux.

Validated build evidence:

```text
Docker Buildroot rebuild after linux-dirclean: ok
./scripts/validate-fruitjam-examples.sh: ok
./scripts/validate-fruitjam-image.sh buildroot-output-docker-images: ok
```

The latest replacement image now also includes the wili8jam-style PRE path for
low-speed devices behind a full-speed hub:

```text
0103-misc-add-fruitjam-hcd-pre-for-low-speed-hub-devices.patch
0104-misc-finish-fruitjam-hcd-pre-program-selection.patch
0105-misc-wait-for-fruitjam-hcd-pre-tx-idle.patch

low-speed hub child URBs:
  send full-speed PRE PID
  wait for the PRE transmitter's post-PRE TXSTALL/idle point
  switch the child packet to low-speed timing
  keep upstream full-speed idle polarity while behind the hub
  restore the full-speed bus after the transaction
```

This is taken from the same wili8jam reference point as above. On 2026-06-18,
the local clone and upstream GitHub `HEAD` / tag `v1.1` all resolved to:

```text
71104dda2ac448fe2eb6c0c62f988eb6154faa78
```

Current PRE/recovery image:

```text
buildroot-output-docker-images/flash-image.uf2
sha256 2d03e2360dd19d51f245526e66e30031f01e1723cda5d1e73849327c20690b9a

buildroot-output-docker-images/flash-image.bin
sha256 5b6f68dd6b43c87fda8703e26e6a91f58a0bc80b203b6aec2c90bd1a1e0e2f33

buildroot-output-docker-images/Image
sha256 4d98bc837abf47d46cdb8ba4f99f1c560a6e3acfd65d0409fc78f5d1d8cfa5cd

buildroot-output-docker-images/bootloader.bin
sha256 e6f620b9780aa1645a6c30552a1327049f3c95bfae3b3585f4f57ee8ca9cdaf8

buildroot-output-docker-images/rootfs.tar
sha256 0549dc5ffd1f6114f7aa553de562352481b0e16dd3fd783466286e6b568973c2
```

Hardware state immediately after this rebuild:

```text
picotool info -a
No accessible RP2040/RP2350 devices in BOOTSEL mode were found.
```

So this image is ready for the one manual flash now, but the repaired recovery
paths still need to be verified on hardware after it boots: first
`fruitjamctl bootsel`/CDC 1200-baud/telnet BOOTSEL, then the Logitech keyboard
receiver and Xbox 360 receiver HCD smoke checks above.

Current live run against the old image:

```text
python3 scripts/usbhost-hcd-smoke.py --transport telnet --telnet-host 192.168.1.7 --telnet-port 23 -v
$ echo FJ_HCD_READY
FAIL shell preflight  rc=None telnet error: [Errno 54] Connection reset by peer
0 passed, 1 failed

python3 scripts/usbhost-hcd-smoke.py --transport cdc --port /dev/cu.usbmodem101 --serial-open-timeout 3 -v
FAIL shell preflight  serial open timed out after 3s on /dev/cu.usbmodem101
0 passed, 1 failed
```

Direct pyserial open on `/dev/cu.usbmodem101` later hung in
`termios.tcsetattr` and was interrupted from the host. Read-only HTTP probes
also reset on the same old image:

```text
curl --max-time 5 -sS 'http://192.168.1.7/cgi-bin/fruitjam.cgi?action=status'
curl: (56) Recv failure: Connection reset by peer

curl --max-time 5 -sS 'http://192.168.1.7/cgi-bin/fruitjam.cgi?action=usbhost'
curl: (56) Recv failure: Connection reset by peer
```

After adding the same subprocess CDC-open probe to the focused keyboard smoke
script that already existed in `scripts/usbhost-hcd-smoke.py`, the focused
keyboard smoke failed cleanly instead of wedging the host process:

```text
python3 scripts/usbhost-keyboard-smoke.py \
  --transport cdc \
  --port /dev/cu.usbmodem101 \
  --serial-open-timeout 3 \
  --seconds 1 \
  --require-input \
  -v

$ echo FJ_SHELL_READY
rc=None timed_out=True
FAIL board shell preflight  timeout after 5s
0 passed, 1 failed

picotool info -a
No accessible RP2040/RP2350 devices in BOOTSEL mode were found.
```

Bark notification was not sent because neither `FJ_BARK_URL` nor `BARK_URL` is
set in the host environment.

The recovery helper now also runs pyserial CDC shell and CDC 1200-baud recovery
inside bounded child processes, so a bad macOS CDC open cannot wedge the parent
flasher. It also has `--watch-only`, which only polls `picotool` and sends no
HTTP, telnet, CDC-shell, or 1200-baud recovery trigger.

```text
python3 scripts/fruitjam-recover-flash.py --watch-only --manual-bootsel-timeout 2 --no-flash -v
+ picotool info -a
No accessible RP2040/RP2350 devices in BOOTSEL mode were found.
watch-only: waiting up to 2s for BOOTSEL; no recovery triggers will be sent
RP2350 BOOTSEL did not become visible

python3 scripts/fruitjam-recover-flash.py --watch-only --manual-bootsel-timeout 240 -v
watch-only: waiting up to 240s for BOOTSEL; no recovery triggers will be sent
RP2350 BOOTSEL did not become visible
```

Software-only recovery was retried against the same old running image with the
bounded helper and manual waiting disabled:

```text
python3 scripts/fruitjam-recover-flash.py \
  --telnet-host 192.168.1.7 \
  --port /dev/cu.usbmodem101 \
  --manual-bootsel-timeout 0 \
  --post-trigger-bootsel-timeout 20 \
  -v

HTTP BOOTSEL request: socket closed/reset after request
telnet immediate BOOTSEL: three attempts sent; each reset the socket
prompt-aware telnet BOOTSEL: reset
CDC shell BOOTSEL: command sequence sent on /dev/cu.usbmodem101
CDC 1200-baud touch: /dev/cu.usbmodem101 stty and child pyserial timed out
CDC counterpart recovery: /dev/tty.usbmodem101 shell timed out, stty 1200 sent,
  child pyserial timed out
picotool info -a: no accessible BOOTSEL device
```

After rebuilding the `0105` PRE-TX-idle image, the same bounded automatic flash
attempt was repeated with the new UF2:

```text
python3 scripts/fruitjam-recover-flash.py \
  --uf2 buildroot-output-docker-images/flash-image.uf2 \
  --telnet-host 192.168.1.7 \
  --port /dev/cu.usbmodem101 \
  --manual-bootsel-timeout 0 \
  --post-trigger-bootsel-timeout 20 \
  --flash-timeout 120 \
  -v

HTTP BOOTSEL request: socket closed/reset after request
telnet immediate BOOTSEL: three attempts sent; each reset the socket
prompt-aware telnet BOOTSEL: reset
CDC shell BOOTSEL: timed out on both /dev/cu.usbmodem101 and /dev/tty.usbmodem101
CDC 1200-baud touch: stty 1200 succeeded on /dev/cu.usbmodem101, but the old
  image still did not enter BOOTSEL; pyserial touch and tty-side stty timed out
picotool info -a: no accessible BOOTSEL device
visible CDC nodes after attempt: /dev/cu.usbmodem101 and /dev/tty.usbmodem101
```

Conclusion for this checkpoint: source and artifacts now contain the recovery
watchdog/PRE fixes, including the wili8jam-style PRE TX idle wait. The host
helper cannot wedge on bad CDC opens, and host validation passes, but the board
is still running the old image. USB host keyboard/gamepad completion remains
unverified until the new UF2 is flashed once and the post-flash HCD smoke tests
can run.

## 2026-06-18 USB-only post-flash status

The `0105` PRE-TX-idle image was flashed and Linux came back up with the USB
CDC gadget visible on macOS:

```text
/dev/cu.usbmodem101
/dev/tty.usbmodem101

pyserial list_ports:
/dev/cu.usbmodem101 Gadget Serial v2.4 USB VID:PID=0525:A4A7 LOCATION=0-1
manufacturer: Linux 6.15.0 with rp2040-udc
product: Gadget Serial v2.4
```

This proves the board is running Linux again, but it does not prove USB host
enumeration. The USB host smoke test could not complete because target command
channels did not return stable command output:

```text
python3 scripts/usbhost-hcd-smoke.py \
  --transport telnet \
  --telnet-host 192.168.1.7 \
  --telnet-port 23 \
  -v

shell preflight:
  echo FJ_HCD_READY -> FJ_HCD_READY, rc=0

then timed out while collecting:
  uname -a
  fruitjam-usbhost status
  cat /sys/bus/usb/devices/usb1/product
  cat /sys/bus/usb/devices/usb1/speed
  cat /sys/bus/usb/devices/usb1/maxchild
```

Direct short telnet attempts also failed to provide command output:

```text
port 23: open
ports 80, 2000, 2323, 8080: connection refused

printf echo/exit into nc 192.168.1.7 23:
  no shell output on three attempts

raw socket commands:
  echo USB_ONE                  -> timeout
  fruitjam-usbhost status       -> timeout
  ls /sys/bus/usb/devices       -> empty output
  ls /dev/input                 -> timeout
  cat /proc/bus/input/devices   -> timeout
```

CDC did not provide a usable command path either:

```text
python3 scripts/usbhost-hcd-smoke.py \
  --transport cdc \
  --port /dev/cu.usbmodem101 \
  --serial-open-timeout 8 \
  --allow-root-only \
  -v

FAIL shell preflight  serial open timed out after 8s on /dev/cu.usbmodem101

python3 scripts/usbhost-hcd-smoke.py \
  --transport cdc \
  --port /dev/tty.usbmodem101 \
  --serial-open-timeout 8 \
  --allow-root-only \
  -v

FAIL shell preflight  serial open timed out after 8s on /dev/tty.usbmodem101
```

Current USB conclusion: the Linux USB device side is alive, and the USB host
source/image includes the HCD, HID, input, xpad, XInput-sized TX buffer, and
PRE patches through `0105`. There is still no authoritative post-flash evidence
that the CH334F hub, Logitech receiver, or Xbox 360 receiver enumerated, because
the available target command paths did not return the USB registry/input data.

## 2026-06-18 USB-only fresh `0106` hardware status

Fresh flashed image:

```text
buildroot-output-docker-images/flash-image.uf2
sha256 ce17657469943aa92dca5580a316705044e461099863627ce7ba84f2d00a0e9c

Linux (none) 6.15.0 #1 Thu Jun 18 10:36:27 UTC 2026 riscv32
```

After this flash, telnet and HTTP came back and the HCD smoke test could collect
real target state:

```text
python3 scripts/usbhost-hcd-smoke.py \
  --transport telnet \
  --telnet-host 192.168.1.7 \
  --telnet-port 23 \
  -v

11 passed, 5 failed
```

Passed:

```text
shell preflight
kernel
usbhost bridge
root hub product
root hub speed
root hub ports
usb sysfs devices
input devices check
input registry
usb device registry
hcd root hub
```

Failed:

```text
external usb packets
logitech receiver usb
hid keyboard input
xbox receiver usb
xpad gamepad input
```

Exact USB state:

```text
usbhost power on gpio11=1 gpio1(dp)=1 gpio2(dm)=0
usbhost device full-speed-device
pio-ready yes
pio-configured yes
pio-debug index 0 sm-tx 0 sm-rx 1 sm-eop 2 clk-sys-hz 252000000
pio-clkdiv tx 0x00054000 rx 0x00010000 eop 0x0002a000
packets 3985 tx-errors 0 last-tx-result 0 len 9
dma present 1 channel 9 packets 4001 last-result 0 ctrl 0x00812013
rx-attempts 144 rx-errors 144 last-rx-result -110 pid 0x00 len 0
probe-summary hcd-control no-data failed modes=upstream,arm-gap gaps=4,2,1,0,8,12,16,6 last-ret=-110 pid=0x00 rx-len=0
dp/dm pad 0x77, controls 0x00010006
```

Linux only saw the synthetic/root HCD device:

```text
ls /sys/bus/usb/devices
1-0:1.0 usb1

fruitjam-usbhost usb-devices
usb1 1d6b:0001 ... Fruit Jam RP2350 PIO USB host speed=12 maxchild=1
1-0:1.0 -:-
```

Low-level local PIO probes still passed:

```text
fruitjam-usbhost self-rx
  last-rx-hex 014b4dc6
  DATA1 zero-payload decode valid after one prefix byte

fruitjam-usbhost setup-token-self-rx
  last-rx-hex 802d0010
  SETUP token decode valid

fruitjam-usbhost setup-data-self-rx-drain
  last-rx-hex 80c38006000100000800eb94
  DATA0 setup payload decode valid
```

External EP0 probes still did not return a valid hub response:

```text
fruitjam-usbhost get-device-8
  timed out with -110 and no RX bytes

fruitjam-usbhost get-device-8-combo
  protocol error
  last-rx-result -71 pid 0xf7 len 12
  last-rx-hex e1f7fbfd7ebfdfe35dc130c4

fruitjam-usbhost get-device-8-combo-skipack
  timed out with -110

fruitjam-usbhost reset-get-device-8-frame-retry 16
  timed out with -110, last-rx-len 0, pid 0x00

fruitjam-usbhost hub-set-address-capture
  captured shifted host SETUP echo only
  last-rx-hex 015b0020
```

Runtime VBUS off/on and a 200 ms bus reset restored full-speed line state but
did not change EP0 behavior. The later `hub-set-address-skipack/noreset` probes
wedged command services; avoid those as routine validation. Plain target
`dmesg` was also not useful on this image because it exited with rc 139 and no
output.

Source verification after this test:

```text
CONFIG_INPUT_JOYDEV=y
CONFIG_INPUT_EVDEV=y
CONFIG_HID=y
CONFIG_HID_GENERIC=y
CONFIG_USB_HID=y
CONFIG_JOYSTICK_XPAD=y
```

So keyboard/gamepad driver support is present in Linux. The current blocker is
earlier: the CH334F hub does not enumerate as `1-1`, so the Logitech receiver
and Xbox 360 receiver never reach HID/xpad binding.

## 2026-06-18 `0107` HCD setup-handshake scan image

New source patch:

```text
0107-misc-scan-fruitjam-hcd-setup-handshakes.patch
```

This patch adds a bounded HCD handshake scanner for control handshakes. It drains
the RX FIFO for the existing receive timeout, ignores reflected non-handshake
bytes, clears transient RX IRQs while scanning, and accepts the known
Pico-PIO-USB issue-97 one-bit-shifted form of ACK/NAK/STALL for the setup
handshake only. This is meant to test whether the only remaining first-EP0
problem is a shifted setup ACK or a valid ACK hidden behind reflected host bytes.

Build and validation:

```text
Docker Buildroot linux-dirclean rebuild: ok
./scripts/validate-fruitjam-examples.sh: ok
./scripts/validate-fruitjam-image.sh buildroot-output-docker-images: ok
```

Exported artifact hashes:

```text
buildroot-output-docker-images/flash-image.bin
sha256 51d73518f9f42a32f0e1efe28cd4120d64037581d97044a135e2f287c8f57fd8

buildroot-output-docker-images/flash-image.uf2
sha256 1bb120e262548205bb3700065682691617fea51058e0f33972bdbd6ed13325b8

buildroot-output-docker-images/Image
sha256 626e61bebf13c91c48314a4d31b66edd038866af129f31933f51bd5db3a8b3c8

buildroot-output-docker-images/rootfs.tar
sha256 3fe37896ea3b2e1b34840231f38d189349355b3c6f918b5ab7c7e7136b04e78d

buildroot-output-docker-images/bootloader.bin
sha256 e6f620b9780aa1645a6c30552a1327049f3c95bfae3b3585f4f57ee8ca9cdaf8
```

Flash status for this checkpoint:

```text
picotool info -a
No accessible RP2040/RP2350 devices in BOOTSEL mode were found.

python3 scripts/fruitjam-recover-flash.py -v \
  --uf2 buildroot-output-docker-images/flash-image.uf2 \
  --http-host 192.168.1.7 \
  --telnet-host 192.168.1.7 \
  --manual-bootsel-timeout 0 \
  --post-trigger-bootsel-timeout 45 \
  --flash-timeout 120
```

Automated recovery result:

```text
HTTP 192.168.1.7:80: connection refused
telnet 192.168.1.7:23: accepted immediate commands, then reset sockets
CDC shell /dev/cu.usbmodem101: timed out
CDC stty 1200 on /dev/cu.usbmodem101: timed out
CDC pyserial 1200-baud DTR-low touch on /dev/cu.usbmodem101: sent
CDC shell /dev/tty.usbmodem101: timed out
CDC stty 1200 on /dev/tty.usbmodem101: timed out
CDC pyserial 1200-baud DTR-low touch on /dev/tty.usbmodem101: sent
picotool info -a: no accessible BOOTSEL device
```

A 240-second `picotool` watch loop also did not see ROM BOOTSEL, so this `0107`
image has not yet been hardware-flashed or USB-smoked. A Bark notification was
sent successfully:

```text
{"code":200,"message":"success","timestamp":1781781557}
```

Follow-up after the board was manually handled at 2026-06-18T11:28:18Z:

```text
picotool info -a: no accessible RP2040/RP2350 devices in BOOTSEL mode
/dev/cu.usbmodem101 and /dev/tty.usbmodem101: still present
diskutil list: no external UF2/RP2350 mass-storage volume
system_profiler SPUSBDataType: no RP2350/Raspberry/Fruit/Pico match
```

Direct CDC shell access was attempted on `/dev/cu.usbmodem101`, but macOS hung
inside the serial `tcsetattr` path before any command could be sent. The process
had to be interrupted.

Direct telnet BOOTSEL payloads were then attempted against `192.168.1.7:23`:

```text
bootsel 250\r\n: socket reset, then picotool did not see BOOTSEL
bootsel\r\n: socket reset, then picotool did not see BOOTSEL
/usr/bin/fruitjamctl bootsel\r\n: socket reset, then picotool did not see BOOTSEL
```

The `0107` UF2 is still only built/validated on the host. It is not flashed on
the board yet, so all current USB hardware status remains the last flashed
`0106` result above.

## 2026-06-18 `0108` passive HCD setup-handshake scan image

New source patch:

```text
0108-misc-keep-fruitjam-hcd-handshake-scan-passive.patch
```

Reason for the patch:

```text
Pico-PIO-USB's RX PIO uses IRQ4 as DECODER_TRIGGER while a packet is being
decoded. The `0107` HCD setup-handshake scanner cleared RX_ALL inside its scan
loop, which could clear that live decoder trigger while trying to observe the
setup ACK/data response from the CH334F. The `0108` patch makes the scanner
passive: receive state is cleared before the scan starts, then the scanner only
drains RX FIFO words and observes IRQ state.
```

Build and validation:

```text
Docker Buildroot linux-dirclean rebuild: ok
./scripts/validate-fruitjam-examples.sh: ok
./scripts/validate-fruitjam-image.sh buildroot-output-docker-images: ok
```

Exported artifact hashes:

```text
buildroot-output-docker-images/flash-image.bin
sha256 dbf5499994cf3b17b3046647cb68a3a4a362e661d1431a832e1eeebcded1d250

buildroot-output-docker-images/flash-image.uf2
sha256 a6f97ff9eeecf77302cc8acb11ec478a5ee0cff04d0a661160aafeec7a127508
```

Flash status for this checkpoint, recorded at 2026-06-18T11:40:39Z:

```text
picotool info -a
No accessible RP2040/RP2350 devices in BOOTSEL mode were found.

ls /dev/cu.usb* /dev/tty.usb*
/dev/cu.usbmodem101
/dev/tty.usbmodem101

diskutil list external physical: no external UF2/RP2350 mass-storage volume
system_profiler SPUSBDataType: no RP2350/Raspberry/Fruit/Pico match
```

Automated recovery/flash attempt:

```text
python3 scripts/fruitjam-recover-flash.py -v \
  --uf2 buildroot-output-docker-images/flash-image.uf2 \
  --http-host 192.168.1.7 \
  --telnet-host 192.168.1.7 \
  --manual-bootsel-timeout 0 \
  --post-trigger-bootsel-timeout 45 \
  --flash-timeout 120
```

Result:

```text
HTTP 192.168.1.7:80: connection refused
telnet 192.168.1.7:23: accepted immediate commands, then reset sockets
CDC shell /dev/cu.usbmodem101: timed out
CDC stty 1200 on /dev/cu.usbmodem101: timed out
CDC shell /dev/tty.usbmodem101: timed out
CDC stty 1200 on /dev/tty.usbmodem101: sent once, but did not expose ROM loader
picotool info -a: no accessible BOOTSEL device
```

A Bark notification was sent successfully after all software trigger paths
failed:

```text
{"code":200,"message":"success","timestamp":1781782835}
```

The `0108` UF2 is built and host-validated but not flashed yet. Current USB
hardware status therefore remains the last flashed `0106` result: root hub only,
external CH334F hub/device EP0 enumeration failing before Logitech HID or Xbox
360 xpad binding can occur.

Follow-up after the board later appeared as RP2350 ROM BOOTSEL:

```text
picotool info -a: RP2350 boot type bootsel, flash size 16384K
picotool load -fu buildroot-output-docker-images/flash-image.uf2: ok
picotool reboot: ok
```

The flashed `0108` image booted and telnet was reachable:

```text
Linux (none) 6.15.0 #1 Thu Jun 18 11:35:04 UTC 2026 riscv32
fruitjam-usbhost status:
  power on gpio11=1 gpio1(dp)=1 gpio2(dm)=0
  device full-speed-device
  pio-ready yes
  dma present 1 channel 9
  rx-attempts 288 rx-errors 144 last-rx-result -71 pid 0x00 len 0
  probe-summary hcd-control no-data failed modes=upstream,arm-gap gaps=4,2,1,0,8,12,16,6 last-ret=-71 pid=0x00 rx-len=0
```

HCD smoke on `0108`:

```text
python3 scripts/usbhost-hcd-smoke.py --transport telnet --telnet-host 192.168.1.7 --telnet-port 23 -v

10 passed, 6 failed
PASS: shell preflight, kernel, usbhost bridge, root hub product, root hub speed,
      root hub ports, usb sysfs devices, input registry, usb device registry,
      hcd root hub
FAIL: input devices timeout, external usb packets, Logitech receiver USB,
      HID keyboard input, Xbox receiver USB, xpad gamepad input
```

Focused `0108` probes:

```text
fruitjam-usbhost self-rx:
  last-rx-result 0 pid 0x4b len 4
  last-rx-hex 014bf331

fruitjam-usbhost setup-token-self-rx:
  last-rx-result 0 pid 0x2d len 4
  last-rx-hex 802d0010

fruitjam-usbhost setup-data-self-rx-drain:
  last-rx-result 0 pid 0xc3 len 12
  last-rx-hex 80c38006000100000800eb94

fruitjam-usbhost get-device-8:
  failed -110, pid 0x00 len 0

fruitjam-usbhost get-device-8-combo:
  failed -71, pid 0xf7 len 12
  last-rx-hex e1f7fbfd7ebfdfef78cb30c0

fruitjam-usbhost reset-get-device-8-frame-retry 16:
  failed -110, pid 0x00 len 0

fruitjam-usbhost hub-set-address-upstream 0:
  failed -71, pid 0x00 len 0
  probe-summary hub-set-address-upstream setup-handshake pid=0x00 len=0 sof-frames=0 ret=0
```

Interpretation: `0108` preserved local PIO transmit/receive decode, but the
external CH334F EP0 still did not answer with a usable ACK or descriptor. The
`get-device-8-combo` packet remains a shifted host SETUP/DATA0 echo pattern, not
a hub response. Logitech HID and Xbox xpad are still blocked by hub EP0
enumeration.

## 2026-06-18 `0109` tight HCD handshake-window image

New source patch:

```text
0109-misc-tighten-fruitjam-hcd-handshake-scan-window.patch
```

Reason for the patch:

```text
Pico-PIO-USB waits for RX_START within a short USB handshake window and then
drains only the immediate handshake packet window. The `0108` scanner was
passive but still broad enough to observe late reflected host bytes. `0109`
tightens the HCD scanner to require RX_START within 3 us for full speed or
12 us for low speed, then drain a 7 us packet window while preserving the
one-bit-shifted ACK/NAK/STALL tolerance.
```

Build and validation:

```text
Docker Buildroot linux-dirclean rebuild: ok
./scripts/validate-fruitjam-examples.sh: ok
./scripts/validate-fruitjam-image.sh buildroot-output-docker-images: ok
```

Exported artifact hashes:

```text
buildroot-output-docker-images/flash-image.bin
sha256 79fd64039aa82a20d62acb448d23c1ded6d327efa67f6faed75a4fd1c6d8b660

buildroot-output-docker-images/flash-image.uf2
sha256 434edaa71193a95747315327227cff3afe8a8c770203423b40bd6026d4acfea1

buildroot-output-docker-images/Image
sha256 b7209421fa782503cdc81af8a88710ff2ce031fad812955df61e28b3edb35cf9
```

Flash status for this checkpoint, recorded at 2026-06-18T11:58:43Z:

```text
picotool info -a
No accessible RP2040/RP2350 devices in BOOTSEL mode were found.

python3 scripts/fruitjam-recover-flash.py -v \
  --uf2 buildroot-output-docker-images/flash-image.uf2 \
  --http-host 192.168.1.7 \
  --telnet-host 192.168.1.7 \
  --manual-bootsel-timeout 0 \
  --post-trigger-bootsel-timeout 60 \
  --flash-timeout 120
```

Automated recovery result:

```text
HTTP 192.168.1.7:80: connection refused
telnet 192.168.1.7:23: direct BOOTSEL commands sent
prompt-aware telnet: fruitjamctl bootsel sent with no prompt before command
CDC shell /dev/cu.usbmodem101: timed out
CDC stty 1200 /dev/cu.usbmodem101: timed out
CDC shell /dev/tty.usbmodem101: timed out
CDC stty 1200 /dev/tty.usbmodem101: timed out
picotool info -a: no accessible BOOTSEL device
```

A Bark notification was sent successfully:

```text
{"code":200,"message":"success","timestamp":1781783919}
```

A final passive `picotool info -a` watch loop ran for 180 seconds after the
notification:

```text
wait 1..60: not visible
Timed out waiting for RP2350 BOOTSEL loader
```

The `0109` UF2 is built and host-validated but not flashed yet. Current USB
hardware status remains the flashed `0108` result above.

## 2026-06-18 `0109` plus stronger recovery trigger artifact

Follow-up source changes after the failed `0109` flash attempt:

```text
package/fruitjam-utils/src/fruitjam-telnetd.c
  local telnet direct BOOTSEL detector now waits up to 1.2 s, re-peeks the
  initial payload, and matches `bootsel` case-insensitively.

package/fruitjam-airlift/src/airliftctl.c
  AirLift inbound telnet direct BOOTSEL detector now uses a 1.2 s initial
  window and accumulates initial socket payload until it can see `bootsel`.

package/fruitjam-utils/src/fruitjamctl.c
  `fruitjamctl bootsel` returns success if the reboot syscall unexpectedly
  returns success.
```

Build and validation:

```text
./scripts/validate-fruitjam-examples.sh: ok
Docker Buildroot package rebuild for fruitjam-utils and fruitjam-airlift: ok
./scripts/validate-fruitjam-image.sh buildroot-output-docker-images: ok
```

Exported artifact hashes:

```text
buildroot-output-docker-images/flash-image.uf2
sha256 9bee0a094dba4c40f5aed226f99def2e520975902810ed54c694f8995e137d91

buildroot-output-docker-images/flash-image.bin
sha256 5f6a6e3aadcd8f7666adbdf526a6037f44934b65999406c67baf3ce513773a66

buildroot-output-docker-images/Image
sha256 b7209421fa782503cdc81af8a88710ff2ce031fad812955df61e28b3edb35cf9
```

Live hardware state after the user reported a BOOTSEL attempt:

```text
picotool info -a:
No accessible RP2040/RP2350 devices in BOOTSEL mode were found.

macOS USB registry:
USB Product Name = "Gadget Serial v2_4"
USB Vendor Name = "Linux 6.15.0 with rp2040-udc"
idVendor = 1317
idProduct = 42151

/dev nodes:
/dev/cu.usbmodem101
/dev/tty.usbmodem101
```

Recovery helper run against the still-flashed image:

```text
python3 scripts/fruitjam-recover-flash.py \
  --uf2 buildroot-output-docker-images/flash-image.uf2 \
  --http-host 192.168.1.7 \
  --telnet-host 192.168.1.7 \
  --port /dev/cu.usbmodem101 \
  --serial-open-timeout 4 \
  --post-trigger-bootsel-timeout 45 \
  --manual-bootsel-timeout 0 \
  -v
```

Observed result:

```text
HTTP 192.168.1.7:80: connection refused
telnet 192.168.1.7:23: immediate BOOTSEL attempt 1 sent
telnet 192.168.1.7:23: later attempts reset by peer
prompt-aware telnet: connection reset by peer
CDC shell /dev/cu.usbmodem101: timed out opening the device
CDC stty 1200 /dev/cu.usbmodem101: timed out
CDC 1200 touch /dev/cu.usbmodem101: timed out
CDC shell /dev/tty.usbmodem101: timed out opening the device
CDC stty 1200 /dev/tty.usbmodem101: timed out
CDC 1200 touch /dev/tty.usbmodem101: timed out
picotool info -a: no accessible BOOTSEL device
```

No hardware UART device was visible on macOS:

```text
only /dev/cu.usbmodem101 and /dev/tty.usbmodem101 were present
```

Direct libusb CDC control attempt:

```text
PyUSB found USB CDC gadget VID:PID 0525:a4a7.
interface 0 class=0x02 subclass=0x02
SET_LINE_CODING 1200 to interface 0 failed:
usb.core.USBError: [Errno 13] Access denied (insufficient permissions)

sudo -n python3 ... failed:
sudo: a password is required
```

Status: this stronger-recovery artifact is built and host-validated, but not
flashed. The board visible to macOS is still Linux CDC, not the ROM loader. USB
keyboard/gamepad status therefore remains the last flashed `0108` hardware
result until this UF2 can be flashed and tested.

## 2026-06-18 flashed `0109` hardware result

The board later appeared in the RP2350 ROM loader and the `0109` plus recovery
artifact was flashed:

```text
picotool info -a:
type RP2350
boot type: bootsel
flash size: 16384K

picotool load -fu buildroot-output-docker-images/flash-image.uf2
The device was rebooted into application mode.
```

Flashed artifact:

```text
buildroot-output-docker-images/flash-image.uf2
sha256 9bee0a094dba4c40f5aed226f99def2e520975902810ed54c694f8995e137d91
```

After reboot, macOS saw the Linux USB gadget again:

```text
/dev/cu.usbmodem101
/dev/tty.usbmodem101

USB Product Name = "Gadget Serial v2_4"
USB Vendor Name = "Linux 6.15.0 with rp2040-udc"
idVendor = 1317
idProduct = 42151
```

Both service ports initially accepted connections:

```text
192.168.1.7:23 telnet: succeeded
192.168.1.7:80 http: succeeded
```

The CDC boot log showed the current USB host failure clearly:

```text
usb 1-1: new full-speed USB device number 2 using fruitjam-usbhost
usb 1-1: device descriptor read/64, error -110
usb 1-1: device descriptor read/64, error -110
usb 1-1: new full-speed USB device number 3 using fruitjam-usbhost
usb 1-1: device descriptor read/64, error -110
usb 1-1: device descriptor read/64, error -110
usb usb1-port1: attempt power cycle
usb 1-1: new full-speed USB device number 4 using fruitjam-usbhost
usb 1-1: device not accepting address 4, error -110
usb 1-1: new full-speed USB device number 5 using fruitjam-usbhost
usb 1-1: device not accepting address 5, error -110
usb usb1-port1: unable to enumerate USB device
```

The host-side `usbhost-hcd-smoke.py` telnet runner got the initial
`FJ_HCD_READY` echo, then timed out on normal commands such as `uname -a`,
`fruitjam-usbhost status`, and USB sysfs reads. A CDC command burst still read
the boot log and showed only the root hub under `/sys/bus/usb/devices`; no
Logitech, Xbox, HID, event, or joystick device reached Linux enumeration.

After the USB host probe attempt, command transport degraded:

```text
raw telnet: accepted TCP connection but returned no shell output
HTTP GET /: connection timed out
CDC serial: open/write path stalled or timed out
picotool info -a: no accessible BOOTSEL device
```

Software recovery attempts from the wedged `0109` image did not reach the ROM:

```text
direct telnet payload "bootsel": no ROM device visible after 20 s
CDC 1200-baud touch on /dev/cu.usbmodem101: timed out
picotool info -a: No accessible RP2040/RP2350 devices in BOOTSEL mode were found.
```

Hardware interpretation for `0109`:

* The Fruit Jam USB host root hub comes up.
* The external downstream path detects a full-speed device on port `1-1`.
* EP0 device-descriptor reads still time out with `-110`.
* Keyboard/gamepad are still blocked before Linux can bind `usbhid`, `hid-generic`,
  or `xpad`.
* Some focused/probe paths can still starve the no-MMU command services badly
  enough that telnet, HTTP, and CDC stop being useful recovery paths.

## 2026-06-18 `0110` Pico-style first SETUP handshake artifact

New source change:

```text
board/raspberrypi/raspberrypi-pico2/patches/linux/
  0110-misc-use-pico-style-hcd-setup-handshake.patch
```

This patch adds a Pico-PIO-USB-style HCD SETUP handshake waiter:

* wait for `RX_START` inside the short full-speed/low-speed handshake window,
* drain only the immediate handshake packet window,
* accept only exact `SYNC` plus ACK/NAK/STALL,
* use this stricter waiter for the first upstream HCD SETUP attempt and for
  `hub-set-address-upstream`,
* keep the later arm-gap fallback attempts on the previous shifted-handshake
  scan path for diagnostics.

Patch validation:

```text
patch -p1 --dry-run against /br-output/build/linux-6.15: ok
```

Build and validation:

```text
Docker linux-dirclean Buildroot rebuild: ok
./scripts/validate-fruitjam-examples.sh: ok
./scripts/validate-fruitjam-image.sh buildroot-output-docker-images: ok
```

Exported artifact hashes:

```text
buildroot-output-docker-images/flash-image.uf2
sha256 c04ee5cc48e8e1b3436a11a487f19467cd50a0be831cf18bf55cc092618c4099

buildroot-output-docker-images/flash-image.bin
sha256 dc4c52d57fc89000e751acac8f711ceddc67e97b014ba3d2e22e6cd0507ec359

buildroot-output-docker-images/Image
sha256 a23602a8bb175735b7a52cdb1b1bb8e981503a84f963da3e340bea5aaef59544
```

Status: `0110` is built and host-validated, but not hardware-flashed yet. The
current board state after the `0109` probe is Linux-gadget-visible but not
command-responsive, and `picotool info -a` does not see the ROM loader.

## 2026-06-18 `0113` wili8jam-default artifact and live USB status

The local wili8jam reference was refreshed from GitHub and still points at
`origin/main` commit `71104dd` (`v1.1`). The relevant known-working Fruit Jam USB
host settings remain PIO USB host on GPIO1/GPIO2, root host port 1, full-speed
host mode, 512-byte enumeration buffer, hub support, four HID slots, four XInput
slots, four host devices, 64-byte HID IN/OUT buffers, `set_sys_clock_pll(...)`
for 252 MHz `clk_sys`, and PIO USB TX DMA channel 9.

New Linux-side source changes beyond the `0110` SETUP handshake artifact:

```text
0111-misc-delay-fruitjam-usbhost-hcd-registration.patch
  Adds delayed HCD registration. The Fruit Jam DTS sets
  raspberrypi,hcd-start-delay-ms = <8000>, giving CDC/telnet/HTTP/userspace a
  service window before automatic Linux USB enumeration can monopolize boot.

0112-misc-fault-fruitjam-hcd-after-repeated-ep0-failures.patch
  Latches the Linux HCD into a visible EP0 fault after repeated failed control
  URBs, reports the root port disconnected, stops HCD SOF, and allows
  fruitjam-usbhost hcd-clear-fault/on/reset to retry. This is a recovery guard,
  not a fix for the CH334F/device EP0 response itself.

0113-misc-default-fruitjam-usbhost-to-wili8jam-config.patch
  Defaults the kernel bridge to the wili8jam electrical configuration:
  PIO0, 252000000 Hz clk_sys, and TX DMA channel 9. The DTS also carries those
  same values explicitly.
```

Current exported artifact hashes:

```text
buildroot-output-docker-images/flash-image.uf2
sha256 9a79fe8e30dbaedda9685149b1a9a80b87c37c940893dcb2f5786cbcce65bb2c

buildroot-output-docker-images/flash-image.bin
sha256 1e4ee2a89f284ea16a0c3e6ba491ca4833143c8f80dca1532b9662adb33ee229

buildroot-output-docker-images/Image
sha256 e734c71188d36fb68a97b0549cdb5a538c148117bd4b28a0b932896066347e8c

buildroot-output-docker-images/adafruit_fruit_jam_rp2350.dtb
sha256 e7a898e82d4d6ef11048b33c1c8bebb578b8f0233d866fde2af9e2629b956f1c
```

Host-side validation passed:

```text
Docker linux-dirclean Buildroot rebuild: ok
./scripts/validate-fruitjam-image.sh buildroot-output-docker-images: ok
./scripts/validate-fruitjam-examples.sh: ok
python3 -m py_compile scripts/usbhost-hcd-smoke.py scripts/fruitjam-recover-flash.py: ok
scripts/usbhost-hcd-smoke.py --self-test -v: 19 passed, 0 failed
```

The HCD smoke test now requires the USB host result that the real goal needs:

```text
hcd not faulted              -> EP0 fault latch must be clear
wili8jam electrical config   -> fruitjam-usbhost status reports PIO0, 252 MHz, DMA9
hcd service window           -> fruitjam-usbhost status reports delay-ms 8000
hcd root hub                 -> usb1 root hub registered
external usb packets         -> PIO RX has non-timeout packet evidence
logitech receiver usb        -> Logitech or 046d:* USB device enumerated
hid keyboard input           -> keyboard event node registered
xbox receiver usb            -> Xbox/xpad USB device enumerated
xpad gamepad input           -> xpad event/js input node registered
```

Fresh live board state while this artifact is still unflashed:

```text
/dev/cu.usbmodem101
/dev/tty.usbmodem101

picotool info -a:
No accessible RP2040/RP2350 devices in BOOTSEL mode were found.

python3 scripts/usbhost-hcd-smoke.py --transport telnet --telnet-host 192.168.1.7 --telnet-port 23 -v:
FAIL shell preflight  rc=None telnet error: [Errno 54] Connection reset by peer

python3 scripts/usbhost-hcd-smoke.py --transport cdc --port /dev/cu.usbmodem101 --serial-open-timeout 3 -v:
FAIL shell preflight  serial open timed out after 3s on /dev/cu.usbmodem101
```

Automatic-only recovery attempt, with no manual wait:

```text
python3 scripts/fruitjam-recover-flash.py \
  --uf2 buildroot-output-docker-images/flash-image.uf2 \
  --port /dev/cu.usbmodem101 \
  --http-host 192.168.1.7 \
  --telnet-host 192.168.1.7 \
  --serial-open-timeout 3 \
  --bootsel-timeout 5 \
  --post-trigger-bootsel-timeout 5 \
  --manual-bootsel-timeout 0 \
  --flash-timeout 180 \
  --bark-url '' \
  -v
```

Observed results:

```text
HTTP BOOTSEL request: socket reset after request was sent
telnet immediate BOOTSEL attempts: connection reset by peer
CDC USB-control 1200-baud touch: access denied by host USB stack
CDC shell BOOTSEL on /dev/cu.usbmodem101: timed out after 3s
stty 1200-baud touch on /dev/cu.usbmodem101: timed out after 3s
CDC shell BOOTSEL on /dev/tty.usbmodem101: timed out after 3s
stty 1200-baud touch on /dev/tty.usbmodem101: sent
CDC 1200-baud serial touch on /dev/tty.usbmodem101: timed out after 3s
picotool info -a: No accessible RP2040/RP2350 devices in BOOTSEL mode were found.
final result: RP2350 BOOTSEL did not become visible
```

Current USB status:

* Working in source and exported artifact: Linux USB core, HID, evdev, joydev,
  xpad, root-hub HCD registration path, interrupt/bulk IN/OUT support,
  low-speed PRE support, XInput-sized TX buffer, wili8jam PIO0/252 MHz/DMA9
  defaults, delayed HCD registration, and EP0-failure fault guard.
* Working in host tests: USB device/input registry collection and the HCD smoke
  criteria for Logitech keyboard receiver plus Xbox 360/xpad.
* Not yet hardware-proven on the current board: the `0113` artifact has not been
  flashed, so there is no evidence yet that the CH334F hub, Logitech receiver,
  HID keyboard event node, Xbox 360 receiver, or xpad event/js node enumerate on
  the live Fruit Jam.
* Last hardware-tested failure remains EP0 enumeration of the downstream
  full-speed device before Logitech HID or Xbox xpad binding can occur.

## 2026-06-18 `0114` receive-quiesce artifact and live access status

New source change:

```text
0114-misc-quiesce-fruitjam-usbhost-rx-after-receive.patch
```

This patch follows the Pico-PIO-USB/wili8jam transaction tail more closely by
making every HCD receive helper finish through one path:

```text
fj_usbhost_pio_finish_receive()
  fj_usbhost_pio_sm_disable(sm_rx)
  fj_usbhost_pio_record_rx(...)
  fj_usbhost_pio_disable_receive(...)
```

The important ordering is intentional: record the diagnostic receive state
first, then quiesce RX/EOP and clear stale FIFO/IRQ state before the next
SETUP/IN/OUT retry. The affected paths are the fast handshake wait, raw receive,
no-EOP receive stop, capture receive, Pico-style handshake wait, and handshake
scan helper.

Buildroot patch/build status:

```text
make -C buildroot O=/br-output BR2_EXTERNAL=/src linux-dirclean
make -C buildroot O=/br-output BR2_EXTERNAL=/src linux-patch
  0114 applied cleanly after the whole 0001..0113 patch stack

Docker Buildroot full rebuild: ok
./scripts/validate-fruitjam-examples.sh: ok
python3 -m py_compile scripts/usbhost-hcd-smoke.py scripts/fruitjam-recover-flash.py: ok
python3 scripts/usbhost-hcd-smoke.py --self-test: 19 passed, 0 failed
./scripts/validate-fruitjam-image.sh buildroot-output-docker-images: ok
```

Exported artifact hashes:

```text
buildroot-output-docker-images/flash-image.uf2
sha256 14f20319e432c388c8a5d7b1177427e7e6c4dcdbdcf8d8a5dbc2f09ddba39867

buildroot-output-docker-images/flash-image.bin
sha256 ee9a8e71eba681cc84bfbfa6505d110c82fb17e614500f2af3adaba50223742e

buildroot-output-docker-images/Image
sha256 01ccedc45d68c30a62800a8acb2f694990445cbf8b7e571e1e27e873fe05ad2c

buildroot-output-docker-images/adafruit_fruit_jam_rp2350.dtb
sha256 e7a898e82d4d6ef11048b33c1c8bebb578b8f0233d866fde2af9e2629b956f1c
```

Automatic flash attempt against the still-running old image:

```text
picotool info -a:
No accessible RP2040/RP2350 devices in BOOTSEL mode were found.

Visible host devices:
/dev/cu.usbmodem101
/dev/tty.usbmodem101

TCP reachability:
192.168.1.7:23 open
192.168.1.7:80 open
```

The recovery helper was run with the fresh `0114` UF2 and no manual fallback:

```text
python3 scripts/fruitjam-recover-flash.py \
  --uf2 buildroot-output-docker-images/flash-image.uf2 \
  --port /dev/cu.usbmodem101 \
  --http-host 192.168.1.7 \
  --telnet-host 192.168.1.7 \
  --serial-open-timeout 3 \
  --bootsel-timeout 8 \
  --post-trigger-bootsel-timeout 12 \
  --manual-bootsel-timeout 0 \
  --flash-timeout 180 \
  --bark-url '' \
  -v
```

Observed result:

```text
HTTP BOOTSEL request: socket reset after request was sent
telnet immediate BOOTSEL attempts: connection reset by peer
prompt-aware telnet BOOTSEL: connection reset by peer
CDC USB-control 1200-baud touch: access denied by host USB stack
CDC shell BOOTSEL on /dev/cu.usbmodem101: timed out after 3s
stty 1200-baud on /dev/cu.usbmodem101: timed out after 3s
CDC shell BOOTSEL on /dev/tty.usbmodem101: timed out after 3s
stty 1200-baud on /dev/tty.usbmodem101: command sent, but no ROM device appeared
picotool info -a: No accessible RP2040/RP2350 devices in BOOTSEL mode were found.
final result: RP2350 BOOTSEL did not become visible
```

Follow-up live checks showed the old image is still up at the socket/device
level, but command services are not usable:

```text
curl http://192.168.1.7/cgi-bin/fruitjam.cgi?action=status:
Recv failure: Connection reset by peer

curl http://192.168.1.7/cgi-bin/fruitjam.cgi?action=usbhost:
Recv failure: Connection reset by peer

telnet prompt-aware wait of 1.5s:
connected, then Connection reset by peer before any fj$ prompt

CDC /dev/cu.usbmodem101:
open/write can return in some line-control states, but no shell output was read;
one DTR/RTS combination hung in host termios tcsetattr until interrupted.
```

Current USB status after `0114`:

* The new `0114` source and exported UF2 are built and host-validated.
* The live board has not flashed `0114`, so there is still no hardware evidence
  that the Logitech receiver, keyboard event node, Xbox receiver, or xpad input
  node enumerate.
* The last authoritative hardware USB result is still that the root hub/HCD path
  starts, but external EP0 reads to the downstream CH334F/full-speed device time
  out before Linux can bind HID or xpad.
* The current live image is socket-visible but command-starved: telnet, HTTP,
  and CDC do not provide a working shell/status path right now.

## 2026-06-18 raw CDC recovery fallback

The live board still exposes Linux gadget serial and answers ping, but all
command services remain unusable:

```text
picotool info -a:
No accessible RP2040/RP2350 devices in BOOTSEL mode were found.

/dev/cu.usbmodem101 and /dev/tty.usbmodem101 are present.
192.168.1.7 ping: ok
TCP 21/23/80: connect succeeds
FTP 21: no banner before timeout
HTTP 80: connection reset by peer
telnet 23: connection reset by peer before any fj$ prompt
CDC shell: no output from echo/uname probes
```

Host-side CDC behavior:

```text
raw os.open/write to /dev/cu.usbmodem101: write ok, no shell output
raw os.open to /dev/tty.usbmodem101: resource busy in one direct probe
pyserial /dev/cu.usbmodem101 DTR=1 RTS=0: opens, no output
other pyserial DTR/RTS combinations: timed out
CONFIG_MAGIC_SYSRQ: not set, so CDC break/sysrq cannot be used as a reboot path
```

`scripts/fruitjam-recover-flash.py` now has an additional raw CDC fallback:

```text
def cdc_raw_shell_bootsel(...)
  os.open(port, O_RDWR | O_NONBLOCK | O_NOCTTY)
  select() until writable
  write bootsel / fruitjamctl bootsel command payload
```

The full automatic recovery order was retried with this helper change:

```text
HTTP BOOTSEL request: connection reset after request was sent
telnet immediate BOOTSEL attempts: connection reset by peer
prompt-aware telnet BOOTSEL: connection reset by peer
CDC USB-control 1200-baud touch: access denied by host USB stack
CDC pyserial shell BOOTSEL on /dev/cu.usbmodem101: timed out
raw CDC BOOTSEL on /dev/cu.usbmodem101: sent
stty 1200-baud on /dev/cu.usbmodem101: timed out
CDC pyserial shell BOOTSEL on /dev/tty.usbmodem101: timed out
raw CDC BOOTSEL on /dev/tty.usbmodem101: resource temporarily unavailable before the select-loop fix
stty 1200-baud on /dev/tty.usbmodem101: timed out
pyserial 1200-baud touch on /dev/tty.usbmodem101: sent
picotool info -a: No accessible RP2040/RP2350 devices in BOOTSEL mode were found.
final result: RP2350 BOOTSEL did not become visible
```

After tightening the raw write loop, a raw-only recovery run showed:

```text
sent raw CDC BOOTSEL commands over /dev/cu.usbmodem101
CDC raw BOOTSEL write failed on /dev/tty.usbmodem101: raw CDC write timed out
picotool info -a: No accessible RP2040/RP2350 devices in BOOTSEL mode were found.
```

This improves the host-side automation for future stuck states, but it did not
recover the currently running old image. The fresh `0114` UF2 is still not
flashed, and Logitech/Xbox USB host validation is still pending on hardware.

## 2026-06-18 0115 manual HCD start image

Problem being addressed:

* `0111` delayed Linux HCD registration by 8 seconds, but the kernel still
  started USB enumeration automatically after the delay.
* On the currently running old image, that can still leave CDC/telnet/HTTP
  command-starved after boot if downstream EP0 enumeration wedges.
* The next image should boot with `/dev/fruitjam-usbhost` available, but should
  not register the Linux HCD until an explicit `fruitjam-usbhost hcd-start`.

Source changes:

```text
0115-misc-add-fruitjam-usbhost-manual-hcd-start.patch
  adds raspberrypi,hcd-manual-start
  reports hcd_manual_start in bridge status
  adds a bridge write command: hcd-start
  calls usb_add_hcd() only after dropping the driver mutex

adafruit_fruit_jam_rp2350.dts
  raspberrypi,hcd-start-delay-ms = <8000>;
  raspberrypi,hcd-manual-start;

fruitjam-usbhost
  exposes hcd-start
  parses hcd_manual_start
  prints: usbhost hcd registered N manual-start N ...

usbhost-hcd-smoke.py
  records pre-start status
  runs: fruitjam-usbhost hcd-start; sleep 2; fruitjam-usbhost status
  then checks usb1 root hub, Logitech receiver, keyboard input, Xbox receiver,
  and xpad input registry evidence
```

Validation:

```text
./scripts/validate-fruitjam-examples.sh: ok
python3 -m py_compile scripts/usbhost-hcd-smoke.py scripts/fruitjam-recover-flash.py: ok
python3 scripts/usbhost-hcd-smoke.py --self-test: 20 passed, 0 failed
cc -Wall -Wextra -Wno-deprecated-declarations -Os fruitjam-usbhost.c: ok
Docker Buildroot linux-dirclean + linux-patch: ok, 0115 applies cleanly
Docker Buildroot full rebuild after fruitjam-utils-dirclean + linux-dirclean: ok
./scripts/validate-fruitjam-image.sh buildroot-output-docker-images: ok
```

Exported artifact checksums:

```text
5d2261ecd138d3b7fe3b3d563cb4a7b1c8188ee282d03596d828361873b5a995  buildroot-output-docker-images/flash-image.uf2
03aef7ba2e863d4f99efcae0bc2b5f7e7937c3fc3021a18049565a50c4864f7a  buildroot-output-docker-images/flash-image.bin
8ab45f80a40de490551ca884340ae972fb200c360ad9139f43984b24ac226aba  buildroot-output-docker-images/Image
f7501b8a71408474efdcee00a0463a08d9a54f31dc89bea616b2b8f16dc52ebe  buildroot-output-docker-images/adafruit_fruit_jam_rp2350.dtb
```

Artifact spot checks:

```text
strings buildroot-output-docker-images/adafruit_fruit_jam_rp2350.dtb:
  raspberrypi,hcd-start-delay-ms
  raspberrypi,hcd-manual-start

tar -xOf buildroot-output-docker-images/rootfs.tar ./usr/bin/fruitjam-usbhost | strings:
  usbhost hcd registered %d manual-start %d ...
  hcd-start
  hcd_manual_start
  HCD start
```

Automatic flash attempt for this image:

```text
python3 scripts/fruitjam-recover-flash.py \
  --uf2 buildroot-output-docker-images/flash-image.uf2 \
  --port /dev/cu.usbmodem101 \
  --http-host 192.168.1.7 \
  --telnet-host 192.168.1.7 \
  --manual-bootsel-timeout 0 \
  --bark-url '' \
  --post-trigger-bootsel-timeout 12 \
  --verbose
```

Observed result:

```text
picotool info -a: no ROM device
HTTP BOOTSEL request to 192.168.1.7:80: connection reset after request
telnet immediate BOOTSEL attempts to 192.168.1.7:23: connection reset by peer
prompt-aware telnet BOOTSEL: connection reset by peer
CDC USB-control 1200-baud touch: access denied by host USB stack
CDC shell BOOTSEL on /dev/cu.usbmodem101: timed out after 6s
raw CDC BOOTSEL on /dev/cu.usbmodem101: sent
stty 1200-baud on /dev/cu.usbmodem101: sent
pyserial 1200-baud DTR-low touch on /dev/cu.usbmodem101: sent
CDC shell BOOTSEL on /dev/tty.usbmodem101: timed out after 6s
raw CDC BOOTSEL on /dev/tty.usbmodem101: raw CDC write timed out
stty 1200-baud on /dev/tty.usbmodem101: timed out
pyserial 1200-baud touch on /dev/tty.usbmodem101: timed out
final picotool info -a: no ROM device
final result: RP2350 BOOTSEL did not become visible
```

Additional live probe:

```text
TCP 21: accepts, no banner before timeout
TCP 23: accepts, then connection reset by peer
TCP 80: accepts, then connection reset by peer
lsof /dev/cu.usbmodem101 /dev/tty.usbmodem101: no local holder
raw CDC write to /dev/cu.usbmodem101: wrote echo/uname/status payload, no
readable shell output before the host probe was interrupted
```

Current status after `0115`:

* Source and artifacts are ready for the safer manual-HCD-start image.
* The live board is still running the older command-starved image; `0115` is
  not flashed yet.
* Hardware proof is still missing for the real Logitech receiver, keyboard input
  event node, Xbox receiver, and xpad input node.
* Once the board is running the `0115` image, the first USB smoke should be:

```sh
python3 scripts/usbhost-hcd-smoke.py \
  --transport telnet \
  --telnet-host 192.168.1.7 \
  --allow-root-only
```

Then repeat without `--allow-root-only`; completion requires the Logitech and
Xbox checks to pass, not just the root hub.

## 2026-06-18 macOS CDC recovery port tightening

The recovery helper was updated after another live retry showed
`/dev/tty.usbmodem101` only added blocking time on macOS. The helper now tries
`/dev/cu.usbmodem*` by default on Darwin and only tries the `/dev/tty.*`
counterpart if `--include-tty-counterpart` is passed.

Quick source checks:

```text
python3 -m py_compile scripts/fruitjam-recover-flash.py scripts/usbhost-hcd-smoke.py: ok
selected_cdc_ports("/dev/cu.usbmodem101", False): ["/dev/cu.usbmodem101"]
selected_cdc_ports("/dev/cu.usbmodem101", True):
  ["/dev/cu.usbmodem101", "/dev/tty.usbmodem101"]
```

Retried automated flash with the tightened default:

```text
python3 scripts/fruitjam-recover-flash.py \
  --uf2 buildroot-output-docker-images/flash-image.uf2 \
  --port /dev/cu.usbmodem101 \
  --http-host 192.168.1.7 \
  --telnet-host 192.168.1.7 \
  --manual-bootsel-timeout 0 \
  --bark-url '' \
  --post-trigger-bootsel-timeout 12 \
  --verbose
```

Observed result:

```text
picotool info -a: no ROM device
HTTP BOOTSEL request: connection reset by peer after request
telnet immediate BOOTSEL attempts: connection reset by peer
prompt-aware telnet BOOTSEL: connection reset by peer
CDC USB-control 1200-baud touch: access denied by host USB stack
CDC shell BOOTSEL on /dev/cu.usbmodem101: timed out after 6s
raw CDC BOOTSEL on /dev/cu.usbmodem101: sent
stty 1200-baud on /dev/cu.usbmodem101: timed out after 6s
pyserial 1200-baud touch on /dev/cu.usbmodem101: timed out after 6s
final picotool info -a: no ROM device
final result: RP2350 BOOTSEL did not become visible
```

This repairs one host-side recovery rough edge, but it does not change the live
hardware status: the old image still cannot be commanded into ROM from the
available CDC/telnet/HTTP paths.

## 2026-06-18 wili8jam USB config comparison guard

Added `scripts/compare-wili8jam-usb-config.py` so the USB host port can be
checked directly against the local known-working `freewili/wili8jam` checkout
instead of relying only on this note file.

The guard currently verifies these wili8jam reference settings:

```text
usb-host/tusb_config.h:
  CFG_TUH_ENABLED=1
  CFG_TUH_RPI_PIO_USB=1
  BOARD_TUH_RHPORT=1
  CFG_TUH_MAX_SPEED=OPT_MODE_FULL_SPEED
  CFG_TUH_ENUMERATION_BUFSIZE=512
  CFG_TUH_HUB=1
  CFG_TUH_HID=4
  CFG_TUH_XINPUT=4
  CFG_TUH_DEVICE_MAX=4
  CFG_TUH_HID_EPIN_BUFSIZE=64
  CFG_TUH_HID_EPOUT_BUFSIZE=64

src/main.cpp:
  VREG_VOLTAGE_1_30
  set_sys_clock_pll(1260000000, 5, 1)
  GPIO11 VBUS enable
  PIO USB D+ GPIO1
  TX DMA channel 9
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg)
  tuh_task() polling
  XInput callback path

Pico-PIO-USB/src/pio_usb_configuration.h:
  PIO0, SM TX/RX/EOP 0/1/2, 4 devices, 64-byte endpoint packets

usb-host/tusb_xinput/xinput_host.h:
  XInput IN/OUT buffers 64 bytes
  Xbox 360 wireless and wired controller types
```

It then verifies the Linux port mapping:

```text
kernel config:
  CONFIG_FRUITJAM_USBHOST_BRIDGE=y
  CONFIG_USB=y
  CONFIG_HID=y
  CONFIG_HID_GENERIC=y
  CONFIG_USB_HID=y
  CONFIG_INPUT_EVDEV=y
  CONFIG_INPUT_JOYDEV=y
  CONFIG_INPUT_JOYSTICK=y
  CONFIG_JOYSTICK_XPAD=y

Fruit Jam DTS:
  clk_sys 252 MHz
  USB host GPIOs 1/2/11
  USB host PIO0
  SM TX/RX/EOP 0/1/2
  TX DMA channel 9
  hcd-start-delay-ms 8000
  hcd-manual-start

HCD patches and smoke:
  interrupt/bulk/control transfer coverage for HID, XInput, future CDC/MSC
  XInput-sized TX/RX buffers
  low-speed PRE support for keyboard behind a full-speed hub
  explicit fruitjam-usbhost hcd-start
  Logitech receiver and Xbox/xpad smoke expectations
```

Local run:

```text
python3 scripts/compare-wili8jam-usb-config.py --wili8jam-root /Users/fred/Documents/Code/wili8jam
  wili8jam USB config compare: ok (/Users/fred/Documents/Code/wili8jam)
```

`scripts/validate-fruitjam-examples.sh` now compiles and runs this guard when
the local wili8jam checkout is present, and still keeps source greps for the
comparison script itself.

Follow-up validation in the repo:

```text
python3 -m py_compile scripts/compare-wili8jam-usb-config.py scripts/fruitjam-recover-flash.py scripts/usbhost-hcd-smoke.py scripts/usbhost-keyboard-smoke.py: ok
python3 scripts/compare-wili8jam-usb-config.py --wili8jam-root /Users/fred/Documents/Code/wili8jam: ok
python3 scripts/usbhost-hcd-smoke.py --self-test: 20 passed, 0 failed
./scripts/validate-fruitjam-examples.sh: ok
```

Live hardware access check after these source changes:

```text
picotool info -a:
  No accessible RP2040/RP2350 devices in BOOTSEL mode were found.

/dev:
  /dev/cu.usbmodem101
  /dev/tty.usbmodem101

TCP 21:
  connected, no banner before timeout
TCP 23:
  connected, then connection reset by peer
TCP 80:
  connected, then connection reset by peer

python3 scripts/usbhost-hcd-smoke.py --transport cdc --port /dev/cu.usbmodem101 --serial-open-timeout 3 --allow-root-only -v:
  FAIL shell preflight  serial open timed out after 3s on /dev/cu.usbmodem101

python3 scripts/usbhost-hcd-smoke.py --transport telnet --telnet-host 192.168.1.7 --telnet-port 23 --allow-root-only -v:
  FAIL shell preflight  telnet error: [Errno 54] Connection reset by peer
```

Automated recovery/flash retry:

```text
python3 scripts/fruitjam-recover-flash.py \
  --uf2 buildroot-output-docker-images/flash-image.uf2 \
  --port /dev/cu.usbmodem101 \
  --http-host 192.168.1.7 \
  --telnet-host 192.168.1.7 \
  --manual-bootsel-timeout 0 \
  --bark-url '' \
  --post-trigger-bootsel-timeout 8 \
  --verbose
```

Result:

```text
initial picotool info -a: no ROM device
HTTP BOOTSEL: request sent, connection reset by peer
immediate telnet BOOTSEL attempts: connection reset by peer
prompt-aware telnet BOOTSEL: connection reset by peer
CDC USB-control BOOTSEL: access denied by host USB stack
CDC shell BOOTSEL on /dev/cu.usbmodem101: sent text commands
raw CDC BOOTSEL on /dev/cu.usbmodem101: sent
stty 1200-baud on /dev/cu.usbmodem101: timed out after 6s
final picotool info -a: no ROM device
final result: RP2350 BOOTSEL did not become visible
```

The USB host source side is now more tightly tied to wili8jam. The live board
still has no usable shell/ROM path from the host, so the real Logitech/Xbox HCD
smoke remains unrun on hardware for this image.

## 2026-06-18 exported image USB artifact guard

Strengthened `scripts/validate-fruitjam-image.sh` so it validates the exported
artifact, not just source files. The script now reads
`buildroot-output-docker-images/adafruit_fruit_jam_rp2350.dtb` as a flattened
device tree and checks the wili8jam-derived USB host values in the actual DTB:

```text
/clk-sys:
  clock-frequency = 252000000

/fruitjam-pins:
  adafruit,usb-host-gpios = 1 2 11

/soc/usbhost-bridge@d0000000:
  compatible includes adafruit,fruit-jam-rp2350-usbhost
  raspberrypi,dp-gpio = 1
  raspberrypi,dm-gpio = 2
  raspberrypi,power-gpio = 11
  raspberrypi,pio = 0
  raspberrypi,sm-tx = 0
  raspberrypi,sm-rx = 1
  raspberrypi,sm-eop = 2
  raspberrypi,tx-dma-channel = 9
  raspberrypi,clk-sys-hz = 252000000
  raspberrypi,hcd-start-delay-ms = 8000
  raspberrypi,hcd-manual-start present
  status = okay
```

The same image validator now checks the exported rootfs `fruitjam-usbhost`
binary for:

```text
hcd-start
hcd_manual_start
manual-start %d
hcd_faulted
usb-devices
dev-input
input-registry
tx_dma_channel
```

And it checks the exported kernel `Image` for:

```text
fruitjam-usbhost
usbhid: USB HID core driver
hid-generic
xpad_irq_in
xpad_irq_out
Xbox 360 Wireless Receiver (XBOX)
hcd_manual_start %u
raspberrypi,hcd-manual-start
USB host HCD waiting for hcd-start
hcd-start
```

Validation results:

```text
./scripts/validate-fruitjam-image.sh buildroot-output-docker-images:
  ok rootfs buildroot-output-docker-images/rootfs.tar
  ok dtb buildroot-output-docker-images/adafruit_fruit_jam_rp2350.dtb
  ok kernel Image buildroot-output-docker-images/Image
  ok bootloader buildroot-output-docker-images/bootloader.bin
  ok uf2 buildroot-output-docker-images/flash-image.uf2 sha256=5d2261ecd138d3b7fe3b3d563cb4a7b1c8188ee282d03596d828361873b5a995

./scripts/validate-fruitjam-examples.sh:
  ok image USB artifact guard
  Fruit Jam example validation: ok
```

Live recovery retry after the artifact guard:

```text
picotool info -a:
  No accessible RP2040/RP2350 devices in BOOTSEL mode were found.

/dev:
  /dev/cu.usbmodem101
  /dev/tty.usbmodem101

TCP 21:
  connected, no banner before timeout
TCP 23:
  connected, then connection reset by peer
TCP 80:
  connected, then connection reset by peer
```

Bounded automated recovery attempt:

```text
python3 scripts/fruitjam-recover-flash.py \
  --uf2 buildroot-output-docker-images/flash-image.uf2 \
  --port /dev/cu.usbmodem101 \
  --http-host 192.168.1.7 \
  --telnet-host 192.168.1.7 \
  --manual-bootsel-timeout 0 \
  --bark-url '' \
  --post-trigger-bootsel-timeout 8 \
  --verbose
```

Result:

```text
initial picotool info -a: no ROM device
HTTP BOOTSEL request: connection reset by peer after request
immediate telnet BOOTSEL attempts: connection reset by peer
prompt-aware telnet BOOTSEL: connection reset by peer
CDC USB-control BOOTSEL: access denied by host USB stack
CDC shell BOOTSEL on /dev/cu.usbmodem101: timed out after 6s
raw CDC BOOTSEL on /dev/cu.usbmodem101: sent
stty 1200-baud on /dev/cu.usbmodem101: timed out after 6s
final picotool info -a: no ROM device
final result: RP2350 BOOTSEL did not become visible
```

No flash was attempted because the ROM loader never became visible.

## 2026-06-18 wili8jam media guards and live shell status

Added `scripts/compare-wili8jam-media-config.py` beside the USB comparison
script. It verifies the local `/Users/fred/Documents/Code/wili8jam` checkout
still contains the expected working media configuration:

```text
DVI:
  HSTX 640x480@60Hz
  GPIO12..19
  126 MHz HSTX clock from 252 MHz clk_sys
  25.2 MHz pixel timing divider

Audio:
  I2S DIN GPIO24, BCLK GPIO26, WS GPIO27
  I2C SDA GPIO20, SCL GPIO21
  TLV320DAC3100 at 0x18, reset GPIO22
  PIO1, 32-bit MSB-first joined TX FIFO
```

The comparison intentionally keeps Linux's current audio design as a known
working divergence from wili8jam: Linux uses `/dev/fruitjam-audio` to generate
MCLK on GPIO25 at 15 MHz and configures the TLV PLL from MCLK, while wili8jam
uses the I2S BCLK path. Do not remove the Linux MCLK path just to make the code
look closer to wili8jam; that path was the previously audible Fruit Jam Linux
audio path.

`scripts/validate-fruitjam-image.sh` now checks exported video/audio artifacts
in addition to USB:

```text
/fruitjam-pins:
  adafruit,i2c-gpios = 20 21
  adafruit,i2s-gpios = 24 25 26 27 23
  adafruit,dvi-gpios = 12 13 14 15 16 17 18 19

/i2c-gpio0:
  sda-gpios = GPIO20
  scl-gpios = GPIO21
  status = okay

/soc/audio-clock@50300000:
  compatible = adafruit,fruit-jam-rp2350-audio-clock
  pio = 1
  sm = 1
  mclk-sm = 0
  DIN/MCLK/BCLK/WS = 24/25/26/27
  clk-sys-hz = 252000000
  mclk-hz = 15000000

/soc/dvi@400c0000:
  compatible = adafruit,fruit-jam-rp2350-dvi
  reg-names = hstx-ctrl hstx-fifo dma resets io-bank0 pads-bank0 busctrl clocks
  interrupts = 10
```

Validation run after adding the media guard and raw-CDC timeout hardening:

```text
python3 -m py_compile scripts/fruitjam-recover-flash.py \
  scripts/compare-wili8jam-media-config.py \
  scripts/compare-wili8jam-usb-config.py \
  scripts/usbhost-hcd-smoke.py \
  scripts/usbhost-keyboard-smoke.py
  ok

python3 scripts/compare-wili8jam-media-config.py \
  --wili8jam-root /Users/fred/Documents/Code/wili8jam
  wili8jam media config compare: ok (/Users/fred/Documents/Code/wili8jam)

./scripts/validate-fruitjam-image.sh buildroot-output-docker-images
  ok rootfs buildroot-output-docker-images/rootfs.tar
  ok dtb buildroot-output-docker-images/adafruit_fruit_jam_rp2350.dtb
  ok kernel Image buildroot-output-docker-images/Image
  ok bootloader buildroot-output-docker-images/bootloader.bin
  ok uf2 buildroot-output-docker-images/flash-image.uf2 sha256=5d2261ecd138d3b7fe3b3d563cb4a7b1c8188ee282d03596d828361873b5a995

./scripts/validate-fruitjam-examples.sh
  ok wili8jam USB reference compare guard
  ok wili8jam media reference compare guard
  ok image USB/media artifact guard
  Fruit Jam example validation: ok
```

Recovery helper repair:

```text
scripts/fruitjam-recover-flash.py:
  cdc_raw_shell_bootsel now runs the raw os.open/write fallback in a child
  process through run_serial_child(), using --serial-open-timeout.
```

This matters because the current CDC node can hang inside `os.open()`.
Verification against the current stuck CDC node:

```text
recover.cdc_raw_shell_bootsel('/dev/cu.usbmodem101', 2.0, True)
  CDC raw shell BOOTSEL on /dev/cu.usbmodem101 timed out after 2s
  raw_result False
```

Live board USB smoke attempt:

```text
picotool info -a:
  No accessible RP2040/RP2350 devices in BOOTSEL mode were found.

/dev:
  /dev/cu.usbmodem101
  /dev/tty.usbmodem101

lsof /dev/cu.usbmodem101 /dev/tty.usbmodem101:
  no owning process reported

python3 scripts/usbhost-hcd-smoke.py \
  --transport cdc --port /dev/cu.usbmodem101 --serial-open-timeout 5 -v
  FAIL shell preflight serial open timed out after 5s on /dev/cu.usbmodem101

TCP reachability:
  192.168.1.7:23 open
  192.168.1.7:80 open

python3 scripts/usbhost-hcd-smoke.py --transport telnet --telnet-host 192.168.1.7 -v
  FAIL shell preflight rc=None telnet error: [Errno 54] Connection reset by peer

HTTP CGI probes:
  /cgi-bin/fruitjam.cgi?action=status: connection reset by peer
  /cgi-bin/fruitjam.cgi?action=usbhost: connection reset by peer
  /: connection reset by peer
```

Current conclusion: the exported image contains the wili8jam-derived USB config
and the video/audio config guards now pass, but live USB enumeration for the
Logitech receiver and Xbox gamepad is not proven in this run because every board
shell/API transport failed before a USB command could execute.

## 2026-06-18 HCD start power/reset preflight

The previous `fruitjam-usbhost hcd-start` userspace command only wrote
`hcd-start` to `/dev/fruitjam-usbhost`. That registered the Linux HCD but did
not explicitly give the already-plugged CH334F/downstream devices a fresh VBUS
and bus-reset settle window immediately before Linux enumeration.

Changed `package/fruitjam-utils/src/fruitjam-usbhost.c` so the manual
`hcd-start` path now does this before registering HCD, but only when the bridge
reports manual-start mode and HCD is not already registered:

```text
bridge off
wait 250 ms
bridge on
wait 750 ms
bridge reset 100
wait 250 ms
bridge hcd-start
```

This is intentionally tied to the manual HCD start path, not to every
diagnostic probe. It is meant to make the Linux enumeration start more like a
fresh wili8jam boot: VBUS is up and settled before the host stack begins
enumerating the already-connected Logitech receiver / Xbox receiver path.

Source validation:

```text
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only \
  package/fruitjam-utils/src/fruitjam-usbhost.c
  ok

./scripts/validate-fruitjam-examples.sh
  ok usbhost manual HCD start guard
  ok wili8jam USB reference compare guard
  ok wili8jam media reference compare guard
  ok image USB/media artifact guard
  Fruit Jam example validation: ok
```

The first plain Docker rebuild reused the old `fruitjam-utils` local-package
build directory; the UF2 SHA stayed unchanged and the image validator correctly
failed:

```text
./scripts/validate-fruitjam-image.sh buildroot-output-docker-images
  fruitjam-usbhost missing marker b'bridge HCD start power-on failed'
```

Forced `fruitjam-utils-dirclean`, removed stale rootfs/image products, rebuilt,
exported, and converted the image:

```text
docker run --rm \
  -v fruitjam-br-output:/br-output \
  -v "$PWD":/src \
  -w /src \
  debian:13 \
  bash -lc 'set -e; apt-get update >/dev/null; apt-get install -y --no-install-recommends ca-certificates patch git make binutils gcc file wget cpio unzip rsync bc bzip2 g++ perl python3 xz-utils genimage mtools dosfstools cmake >/dev/null; make -C buildroot O=/br-output BR2_EXTERNAL=/src fruitjam-utils-dirclean; rm -f /br-output/build/.root /br-output/build/.rootfs_* /br-output/images/rootfs.* /br-output/images/flash-image.bin /br-output/images/flash-image.uf2; make -C buildroot O=/br-output BR2_EXTERNAL=/src -j$(nproc)'

docker run --rm \
  -v fruitjam-br-output:/br-output \
  -v "$PWD/buildroot-output-docker-images":/dest \
  alpine:latest \
  sh -lc 'mkdir -p /dest; cp -f /br-output/images/flash-image.bin /br-output/images/rootfs.tar /br-output/images/Image /br-output/images/adafruit_fruit_jam_rp2350.dtb /br-output/images/bootloader.bin /dest/'

picotool uf2 convert \
  buildroot-output-docker-images/flash-image.bin \
  buildroot-output-docker-images/flash-image.uf2 \
  --family rp2350-riscv
```

New artifact validation:

```text
shasum -a 256 buildroot-output-docker-images/flash-image.uf2
  c081dc94c3b4e3380e7b337be9ee44e72061099148cfcbb8c9e8f5b602c033d3

./scripts/validate-fruitjam-image.sh buildroot-output-docker-images
  ok rootfs buildroot-output-docker-images/rootfs.tar
  ok dtb buildroot-output-docker-images/adafruit_fruit_jam_rp2350.dtb
  ok kernel Image buildroot-output-docker-images/Image
  ok bootloader buildroot-output-docker-images/bootloader.bin
  ok uf2 buildroot-output-docker-images/flash-image.uf2 sha256=c081dc94c3b4e3380e7b337be9ee44e72061099148cfcbb8c9e8f5b602c033d3
```

Automated recovery / live board state after the new UF2 was built:

```text
picotool info -a:
  No accessible RP2040/RP2350 devices in BOOTSEL mode were found.

/dev:
  /dev/cu.usbmodem101
  /dev/tty.usbmodem101

TCP:
  192.168.1.7:23 open
  192.168.1.7:80 open
```

Recovery helper attempts remained bounded and returned without flashing because
ROM never appeared:

```text
HTTP BOOTSEL request: connection reset by peer after request
immediate telnet BOOTSEL attempts: connection reset by peer
prompt-aware telnet BOOTSEL: connection reset by peer
CDC USB-control BOOTSEL: access denied by host USB stack
CDC shell BOOTSEL: timed out
raw CDC shell BOOTSEL: timed out or sent depending on cu/tty pass
stty/pyserial 1200-baud touches: reported success on some passes
final picotool info -a: no ROM device
```

Live USB smoke still cannot execute target commands:

```text
python3 scripts/usbhost-hcd-smoke.py \
  --transport telnet --telnet-host 192.168.1.7 --allow-root-only -v
  FAIL shell preflight rc=None telnet error: [Errno 54] Connection reset by peer

HTTP:
  /cgi-bin/fruitjam.cgi?action=status: connection reset by peer
  /cgi-bin/fruitjam.cgi?action=usbhost: connection reset by peer
```

Current conclusion: the next flashable UF2 is
`buildroot-output-docker-images/flash-image.uf2` with SHA256
`c081dc94c3b4e3380e7b337be9ee44e72061099148cfcbb8c9e8f5b602c033d3`. It contains
the wili8jam USB/media guard set plus the new `hcd-start` VBUS/reset preflight,
but it has not been flashed or live-smoked because the board never became
visible to `picotool`.

## 2026-06-18 direct HTTPD BOOTSEL endpoint for USB test iteration

The current live image accepts TCP connections on port 80 but resets before
`/cgi-bin/fruitjam.cgi?action=usbhost` or `action=status` can return. Because
`fruitjam-httpd` still routed `action=bootsel` through CGI, the recovery request
could fail at the same no-MMU `vfork`/`exec` point as the status request.

Changed `package/fruitjam-utils/src/fruitjam-httpd.c` so
`/cgi-bin/fruitjam.cgi?action=bootsel` is handled directly by the tiny HTTP
server:

```text
HTTP/1.0 200 OK
Content-Type: application/json
{"ok":true,"accepted":true,"verified":false,"source":"direct-httpd",...}
delay 1200 ms
reboot(..., LINUX_REBOOT_CMD_RESTART2, "bootsel")
```

This matches the existing direct CGI and AirLift JSON semantics but removes one
process launch from the emergency recovery path. It does not change USB host
enumeration behavior; it makes the next USB host image easier to flash and test
without requiring manual intervention.

Source and image guards now require:

```text
fruitjam-httpd source:
  query_has_bootsel_action
  serve_direct_bootsel
  reboot_bootsel_after_delay(1200)
  direct-httpd
  action=bootsel

exported rootfs /usr/sbin/fruitjam-httpd:
  direct-httpd
  action=bootsel
  BOOTSEL request accepted
  picotool info -a
  fruitjam-httpd: reboot bootsel
```

Validation after `fruitjam-utils-dirclean`, rebuild, export, and host-side UF2
conversion:

```text
./scripts/validate-fruitjam-examples.sh
  ok web bootsel direct restart guard
  ok usbhost manual HCD start guard
  ok wili8jam USB reference compare guard
  ok wili8jam media reference compare guard
  Fruit Jam example validation: ok

tar -xOf buildroot-output-docker-images/rootfs.tar ./usr/sbin/fruitjam-httpd | strings:
  action=bootsel
  fruitjam-httpd: reboot bootsel: %s
  {"ok":true,"accepted":true,"verified":false,"source":"direct-httpd",...}

./scripts/validate-fruitjam-image.sh buildroot-output-docker-images:
  ok rootfs buildroot-output-docker-images/rootfs.tar
  ok dtb buildroot-output-docker-images/adafruit_fruit_jam_rp2350.dtb
  ok kernel Image buildroot-output-docker-images/Image
  ok bootloader buildroot-output-docker-images/bootloader.bin
  ok uf2 buildroot-output-docker-images/flash-image.uf2 sha256=d2b39b15dfc559d1a612b9e95ec548bc9786cad4281f3c4da3e3057c17a1e932

shasum -a 256:
  d2b39b15dfc559d1a612b9e95ec548bc9786cad4281f3c4da3e3057c17a1e932  buildroot-output-docker-images/flash-image.uf2
  707c03eb6202ff064fb5709102afa278dcd8d1df0173196293a959e57e8d793f  buildroot-output-docker-images/flash-image.bin
  629359bbea884f5f0121a8725e4e26ff6a5a390fb3dc4e748017d8437816bedf  buildroot-output-docker-images/rootfs.tar
```

Follow-up: the live board trace showed the actual remote recovery path is
AirLift inbound HTTP, not the loopback-only `fruitjam-httpd`. AirLift already
had a direct `action=bootsel` path, but it only called
`reboot_bootsel_after_delay(1500)` when `http_reply_json()` succeeded. In the
observed failure, the TCP connection reset after the request, so a parsed
BOOTSEL request could still skip the reboot if the JSON reply could not be
delivered.

Changed both remote AirLift HTTP and local `fruitjam-httpd` recovery semantics:
once `action=bootsel` has been parsed, the board attempts the restart even if
the HTTP response write fails.

Additional source guard:

```text
AirLift BOOTSEL reboot must not depend on successful HTTP reply delivery
fruitjam-httpd BOOTSEL reboot must not depend on successful HTTP reply delivery
```

Rebuilt with:

```text
make -C buildroot O=/br-output BR2_EXTERNAL=/src \
  fruitjam-airlift-dirclean fruitjam-utils-dirclean
make -C buildroot O=/br-output BR2_EXTERNAL=/src -j$(nproc)
```

Final artifact validation for this pass:

```text
./scripts/validate-fruitjam-examples.sh:
  ok web bootsel direct restart guard
  ok usbhost manual HCD start guard
  ok wili8jam USB reference compare guard
  ok wili8jam media reference compare guard
  Fruit Jam example validation: ok

tar -xOf buildroot-output-docker-images/rootfs.tar ./usr/bin/airliftctl | strings:
  {"ok":true,"accepted":true,"verified":false,"source":"airlift-direct","message":"BOOTSEL request accepted; verify from host with picotool info -a"}
  airliftctl: reboot bootsel: %s

tar -xOf buildroot-output-docker-images/rootfs.tar ./usr/sbin/fruitjam-httpd | strings:
  action=bootsel
  fruitjam-httpd: reboot bootsel: %s
  {"ok":true,"accepted":true,"verified":false,"source":"direct-httpd","message":"BOOTSEL request accepted; verify from host with picotool info -a"}

./scripts/validate-fruitjam-image.sh buildroot-output-docker-images:
  ok rootfs buildroot-output-docker-images/rootfs.tar
  ok dtb buildroot-output-docker-images/adafruit_fruit_jam_rp2350.dtb
  ok kernel Image buildroot-output-docker-images/Image
  ok bootloader buildroot-output-docker-images/bootloader.bin
  ok uf2 buildroot-output-docker-images/flash-image.uf2 sha256=c3f5e6d62c3082abb35cb8fd9cbd090f53afc2caaf385e5618d59fc69264ff92

shasum -a 256:
  c3f5e6d62c3082abb35cb8fd9cbd090f53afc2caaf385e5618d59fc69264ff92  buildroot-output-docker-images/flash-image.uf2
  5981a096a339ec47a2beb2696afbad0221a6f8afc911b73da5cca297f59179d8  buildroot-output-docker-images/flash-image.bin
  49ce058e36ba4dcc8557471c3f5ca86b99255a0b9c783338d5f3296efd73cf3f  buildroot-output-docker-images/rootfs.tar
```

Current live board state after the automatic-only recovery attempt:

```text
picotool info -a:
  No accessible RP2040/RP2350 devices in BOOTSEL mode were found.

/dev:
  /dev/cu.usbmodem101
  /dev/tty.usbmodem101

TCP:
  192.168.1.7:23 open
  192.168.1.7:80 open

python3 scripts/usbhost-hcd-smoke.py --transport telnet --telnet-host 192.168.1.7 --allow-root-only -v:
  FAIL shell preflight rc=None telnet error: [Errno 54] Connection reset by peer

python3 scripts/usbhost-hcd-smoke.py --transport cdc --port /dev/cu.usbmodem101 --serial-open-timeout 5 --allow-root-only -v:
  FAIL shell preflight serial open timed out after 5s on /dev/cu.usbmodem101

HTTP:
  /cgi-bin/fruitjam.cgi?action=status: connection reset by peer
  /cgi-bin/fruitjam.cgi?action=usbhost: connection reset by peer
  /: connection reset by peer
```

The current live image predates the AirLift reply-independent BOOTSEL fix, so
the new UF2 is validated but still not flashed or live-smoked.
