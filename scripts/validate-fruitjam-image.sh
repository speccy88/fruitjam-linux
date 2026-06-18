#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image_dir=${1:-"$repo/buildroot-output-docker-images"}
rootfs="$image_dir/rootfs.tar"
uf2="$image_dir/flash-image.uf2"
kernel_image="$image_dir/Image"
bootloader="$image_dir/bootloader.bin"
dtb="$image_dir/adafruit_fruit_jam_rp2350.dtb"

if [ ! -s "$rootfs" ]; then
	echo "validate-fruitjam-image: missing $rootfs" >&2
	exit 1
fi
if [ ! -s "$uf2" ]; then
	echo "validate-fruitjam-image: missing $uf2" >&2
	exit 1
fi
if [ ! -s "$kernel_image" ]; then
	echo "validate-fruitjam-image: missing $kernel_image" >&2
	exit 1
fi
if [ ! -s "$bootloader" ]; then
	echo "validate-fruitjam-image: missing $bootloader" >&2
	exit 1
fi
if [ ! -s "$dtb" ]; then
	echo "validate-fruitjam-image: missing $dtb" >&2
	exit 1
fi

python3 - "$rootfs" "$uf2" "$kernel_image" "$bootloader" "$dtb" <<'PY'
import hashlib
import struct
import sys
import tarfile
from pathlib import Path

rootfs = Path(sys.argv[1])
uf2 = Path(sys.argv[2])
kernel_image = Path(sys.argv[3])
bootloader = Path(sys.argv[4])
dtb = Path(sys.argv[5])

required = [
    "./bin/rm",
    "./root/berry/00-hello.be",
    "./root/berry/09-mqtt-publish.be",
    "./root/berry/10-mqtt-subscribe.be",
    "./root/berry/11-i2c.be",
    "./root/berry/12-usbhost-keyboard.be",
    "./root/berry/13-airlift.be",
    "./root/berry/14-audio-wav.be",
    "./root/berry/15-board-control.be",
    "./root/berry/run-all.be",
    "./root/berry/neopixel-rainbow-10s.be",
    "./root/rtttl/01-scale.rtttl",
    "./root/sh/15-wav-analyze.sh",
    "./usr/bin/berry",
    "./usr/bin/berry-run",
    "./usr/bin/airliftctl",
    "./usr/bin/fruitjam-berry-json",
    "./usr/bin/fruitjam-dvi",
    "./usr/bin/fruitjam-rtttl",
    "./usr/bin/fruitjam-wavplay",
    "./usr/bin/fruitjam-uptime",
    "./usr/bin/uptime",
    "./usr/bin/fruitjam-du",
    "./usr/bin/du",
    "./usr/bin/fruitjam-ps",
    "./usr/bin/ps",
    "./usr/bin/fruitjam-pgrep",
    "./usr/bin/pgrep",
    "./usr/bin/pkill",
    "./usr/bin/fruitjam-shell",
    "./usr/bin/mosquitto_pub",
    "./usr/bin/mosquitto_sub",
    "./usr/bin/wget",
    "./usr/sbin/fruitjam-ftpd",
    "./usr/sbin/fruitjam-httpd",
    "./usr/sbin/fruitjam-telnetd",
    "./www/cgi-bin/env.cgi",
    "./www/cgi-bin/fruitjam.cgi",
    "./www/index.html",
]

forbidden = [
    "./etc/fruitjam-wifi.conf",
    "./root/neopixels.be",
    "./root/serial-over-tcp.sh",
    "./usr/bin/sqlite3",
]

def read_text(tf: tarfile.TarFile, name: str) -> str:
    data = tf.extractfile(name)
    if data is None:
        raise SystemExit(f"{name}: not a regular file")
    return data.read().decode("utf-8", "replace")

def read_bytes(tf: tarfile.TarFile, name: str) -> bytes:
    data = tf.extractfile(name)
    if data is None:
        raise SystemExit(f"{name}: not a regular file")
    return data.read()

def align4(value: int) -> int:
    return (value + 3) & ~3

def cstring(data: bytes, offset: int) -> str:
    end = data.index(b"\0", offset)
    return data[offset:end].decode("ascii")

def parse_fdt(data: bytes) -> dict[str, dict[str, bytes]]:
    header = struct.unpack(">10I", data[:40])
    magic, _totalsize, off_struct, off_strings, _off_mem, _version, _last, _boot, size_strings, size_struct = header
    if magic != 0xD00DFEED:
        raise SystemExit("DTB has wrong magic")
    strings = data[off_strings:off_strings + size_strings]
    pos = off_struct
    end = off_struct + size_struct
    stack: list[str] = []
    props: dict[str, dict[str, bytes]] = {}

    def current_path() -> str:
        names = [name for name in stack if name]
        return "/" + "/".join(names) if names else "/"

    while pos < end:
        token = struct.unpack_from(">I", data, pos)[0]
        pos += 4
        if token == 1:  # FDT_BEGIN_NODE
            name_end = data.index(b"\0", pos)
            name = data[pos:name_end].decode("ascii")
            pos = align4(name_end + 1)
            stack.append(name)
            props.setdefault(current_path(), {})
        elif token == 2:  # FDT_END_NODE
            if not stack:
                raise SystemExit("DTB ended a node before beginning one")
            stack.pop()
        elif token == 3:  # FDT_PROP
            length, nameoff = struct.unpack_from(">II", data, pos)
            pos += 8
            name = cstring(strings, nameoff)
            value = data[pos:pos + length]
            pos = align4(pos + length)
            props.setdefault(current_path(), {})[name] = value
        elif token == 4:  # FDT_NOP
            continue
        elif token == 9:  # FDT_END
            break
        else:
            raise SystemExit(f"DTB has unknown token {token}")
    return props

def prop(props: dict[str, dict[str, bytes]], path: str, name: str) -> bytes:
    try:
        return props[path][name]
    except KeyError:
        raise SystemExit(f"DTB missing {path} {name}") from None

def prop_u32s(props: dict[str, dict[str, bytes]], path: str, name: str) -> list[int]:
    value = prop(props, path, name)
    if len(value) % 4:
        raise SystemExit(f"DTB {path} {name} length is not a u32 array")
    return list(struct.unpack(f">{len(value) // 4}I", value))

def prop_strings(props: dict[str, dict[str, bytes]], path: str, name: str) -> list[str]:
    return [part.decode("ascii") for part in prop(props, path, name).rstrip(b"\0").split(b"\0") if part]

def require_u32s(props: dict[str, dict[str, bytes]], path: str, name: str, expected: list[int]) -> None:
    actual = prop_u32s(props, path, name)
    if actual != expected:
        raise SystemExit(f"DTB {path} {name} = {actual}, expected {expected}")

def require_string(props: dict[str, dict[str, bytes]], path: str, name: str, expected: str) -> None:
    actual = prop_strings(props, path, name)
    if actual != [expected]:
        raise SystemExit(f"DTB {path} {name} = {actual}, expected {[expected]}")

with tarfile.open(rootfs) as tf:
    names = set(tf.getnames())
    missing = [name for name in required if name not in names]
    if missing:
        raise SystemExit(f"missing rootfs entries: {missing}")
    present_forbidden = [name for name in forbidden if name in names]
    if present_forbidden:
        raise SystemExit(f"forbidden rootfs entries present: {present_forbidden}")

    index = read_text(tf, "./www/index.html")
    for needle in (
        'const requestedHost = queryParams.get("host");',
        '"/cgi-bin/fruitjam.cgi"',
        'data-dvi="dashboard"',
        'data-usbhost="status"',
        'data-usbhost="kbd-auto-shell"',
        'return "SD: " + name.slice(5);',
    ):
        if needle not in index:
            raise SystemExit(f"www/index.html missing {needle!r}")

    run_all = read_text(tf, "./root/berry/run-all.be")
    if 'var BERRY_DIR = "/root/berry"' not in run_all:
        raise SystemExit("run-all.be does not use /root/berry BERRY_DIR")
    for needle in ("09-mqtt-publish.be", "10-mqtt-subscribe.be", "11-i2c.be"):
        if needle not in run_all:
            raise SystemExit(f"run-all.be missing {needle}")

    fruitjam_be = read_text(tf, "./root/berry/fruitjam.be")
    for needle in (
        '"/dev/fruitjam-usbhost"',
        "usbhost_status_from_bridge",
        '"pio_ready"',
        '"last_rx_hex"',
        '"probe_summary"',
        "mqtt_pub_command",
        "mqtt_sub_command",
        "mqtt_publish_script",
        "mqtt_subscribe_script",
        "i2c_scan_command",
        "i2c_ping_command",
        "usbhost_kbd_find_command",
        "usbhost_kbd_auto_shell_command",
        "airlift_tcp_get_command",
        "airlift_join_command",
        "audio_tone_command",
        "rtttl_command",
        "wav_analyze_command",
        "wav_play_command",
        "board_status_command",
        "usb_power_command",
        "periph_reset_command",
        "bootsel_command",
    ):
        if needle not in fruitjam_be:
            raise SystemExit(f"fruitjam.be missing {needle!r}")
    for name, needles in (
        ("./root/berry/09-mqtt-publish.be", ("mqtt_publish_script", "mqtt_pub_command")),
        ("./root/berry/10-mqtt-subscribe.be", ("mqtt_subscribe_script", "mqtt_sub_command")),
    ):
        example = read_text(tf, name)
        for needle in needles:
            if needle not in example:
                raise SystemExit(f"{name} missing {needle!r}")
    i2c_example = read_text(tf, "./root/berry/11-i2c.be")
    for needle in ("i2c_scan_command", "i2c_ping_command", "0x18"):
        if needle not in i2c_example:
            raise SystemExit(f"11-i2c.be missing {needle!r}")
    usbkbd_example = read_text(tf, "./root/berry/12-usbhost-keyboard.be")
    for needle in ("usbhost_kbd_find_command", "usbhost_kbd_auto_text_command",
                   "usbhost_kbd_auto_events_command", "usbhost_kbd_auto_shell_command"):
        if needle not in usbkbd_example:
            raise SystemExit(f"12-usbhost-keyboard.be missing {needle!r}")
    airlift_example = read_text(tf, "./root/berry/13-airlift.be")
    for needle in ("airlift_command", "airlift_tcp_get_command",
                   "airlift_join_command", "example.com"):
        if needle not in airlift_example:
            raise SystemExit(f"13-airlift.be missing {needle!r}")
    audio_example = read_text(tf, "./root/berry/14-audio-wav.be")
    for needle in ("audio_tone_command", "rtttl_command",
                   "wav_analyze_command", "wav_play_command",
                   "fruitjam-scale.wav"):
        if needle not in audio_example:
            raise SystemExit(f"14-audio-wav.be missing {needle!r}")
    ctl_example = read_text(tf, "./root/berry/15-board-control.be")
    for needle in ("board_status_command", "led_command",
                   "usb_power_command", "periph_reset_command",
                   "bootsel_command"):
        if needle not in ctl_example:
            raise SystemExit(f"15-board-control.be missing {needle!r}")

    sh_run_all = read_text(tf, "./root/sh/run-all.sh")
    if "15-wav-analyze.sh" not in sh_run_all:
        raise SystemExit("shell run-all does not include WAV analyzer")

    wavplay = read_bytes(tf, "./usr/bin/fruitjam-wavplay")
    for needle in (b"--analyze", b"backend=", b"segments="):
        if needle not in wavplay:
            raise SystemExit(f"fruitjam-wavplay missing marker {needle!r}")

    web_cgi = read_bytes(tf, "./www/cgi-bin/fruitjam.cgi")
    for needle in (b"direct-cgi", b"berry-list", b"wav-list", b"usbhost",
                   b"kbd-auto-shell", b"/mnt/sd/berry", b"user_scripts", b"user:"):
        if needle not in web_cgi:
            raise SystemExit(f"fruitjam.cgi missing marker {needle!r}")

    httpd = read_bytes(tf, "./usr/sbin/fruitjam-httpd")
    for needle in (b"direct-httpd", b"action=bootsel", b"BOOTSEL request accepted",
                   b"picotool info -a", b"fruitjam-httpd: reboot bootsel"):
        if needle not in httpd:
            raise SystemExit(f"fruitjam-httpd missing marker {needle!r}")

    berry_json = read_bytes(tf, "./usr/bin/fruitjam-berry-json")
    for needle in (b"tiny-berry-json", b"script_source", b"/mnt/sd/berry", b"user:"):
        if needle not in berry_json:
            raise SystemExit(f"fruitjam-berry-json missing marker {needle!r}")

    usbhost = read_bytes(tf, "./usr/bin/fruitjam-usbhost")
    for needle in (
        b"kbd-find",
        b"kbd-auto-shell",
        b"kbd-shell",
        b"kbd-init %u %u %u",
        b"kbd-poll %u %u",
        b"USB keyboard shell",
        b"usbhost keyboard target",
        b"line editing: up/down history",
        b"tab command/path completion",
        b"hcd-start",
        b"hcd_manual_start",
        b"manual-start %d",
        b"hcd_port_reset_settle_ms",
        b"reset-settle-ms %d",
        b"bridge HCD start power-on failed",
        b"bridge HCD start bus-reset failed",
        b"hcd_faulted",
        b"usb-devices",
        b"dev-input",
        b"input-registry",
        b"tx_dma_channel",
    ):
        if needle not in usbhost:
            raise SystemExit(f"fruitjam-usbhost missing marker {needle!r}")

    mqtt_sub = read_bytes(tf, "./usr/bin/mosquitto_sub")
    for needle in (b"mosquitto_sub", b"--airlift", b"-C count", b"-W seconds"):
        if needle not in mqtt_sub:
            raise SystemExit(f"mosquitto_sub missing marker {needle!r}")

    fruitjam_ps = read_bytes(tf, "./usr/bin/fruitjam-ps")
    for needle in (b"fruitjam-ps", b"PID", b"PPID", b"vsize_kb", b"rss_kb"):
        if needle not in fruitjam_ps:
            raise SystemExit(f"fruitjam-ps missing marker {needle!r}")

    fruitjam_uptime = read_bytes(tf, "./usr/bin/fruitjam-uptime")
    for needle in (b"fruitjam-uptime", b"uptime_seconds", b"load_1m", b"load average"):
        if needle not in fruitjam_uptime:
            raise SystemExit(f"fruitjam-uptime missing marker {needle!r}")

    fruitjam_du = read_bytes(tf, "./usr/bin/fruitjam-du")
    for needle in (b"fruitjam-du", b"entries", b"bytes", b"usage:"):
        if needle not in fruitjam_du:
            raise SystemExit(f"fruitjam-du missing marker {needle!r}")

    fruitjam_pgrep = read_bytes(tf, "./usr/bin/fruitjam-pgrep")
    for needle in (b"fruitjam-pgrep", b"pgrep", b"pkill", b"PATTERN"):
        if needle not in fruitjam_pgrep:
            raise SystemExit(f"fruitjam-pgrep missing marker {needle!r}")

    fruitjam_services = read_bytes(tf, "./usr/bin/fruitjam-services")
    for needle in (b"removing stale AirLift monitor pid", b"stale-after=%us",
                   b"AirLift inbound heartbeat stale; restarting"):
        if needle not in fruitjam_services:
            raise SystemExit(f"fruitjam-services missing marker {needle!r}")

    fruitjam_dvi = read_bytes(tf, "./usr/bin/fruitjam-dvi")
    for needle in (
        b"/dev/fruitjam-dvi",
        b"dashboard",
        b"pattern",
        b"show",
        b"start",
        b"stop",
        b"test",
        b"wili-pattern",
        b"wili-show",
        b"wili-test",
    ):
        if needle not in fruitjam_dvi:
            raise SystemExit(f"fruitjam-dvi missing marker {needle!r}")

    fruitjam_rtttl = read_bytes(tf, "./usr/bin/fruitjam-rtttl")
    for needle in (
        b"/dev/fruitjam-audio",
        b"/dev/i2c-0",
        b"--i2s",
        b"--tone",
        b"--waveform",
        b"tone %u %u",
        b"tone %u %u %s",
        b"waveforms: sine square saw triangle noise",
        b"scale:d=8,o=5,b=140",
        b"codec status",
    ):
        if needle not in fruitjam_rtttl:
            raise SystemExit(f"fruitjam-rtttl missing marker {needle!r}")

    airliftctl = read_bytes(tf, "./usr/bin/airliftctl")
    for needle in (b"mqtt-sub", b"mqtt-pub", b"USERNAME PASSWORD",
                   b"AirLift telnet stale session preempted",
                   b"AirLift telnet idle timeout",
                   b"AirLift telnet closed before shell attach; checking BOOTSEL payload",
                   b"AirLift inbound periodic recycle",
                   b"AirLift shell accept failed; recycling",
                   b"AirLift telnet poll failed; closing session",
                   b"AirLift HTTP client failed; keeping inbound server alive",
                   b"AirLift FTP client failed; keeping inbound server alive",
                   b"BOOTSEL request accepted",
                   b"airlift-direct",
                   b"picotool info -a",
                   b"airliftctl: reboot bootsel",
                   b"hcd-start",
                   b"hcd-clear-fault",
                   b"usb-devices",
                   b"dev-input",
                   b"input-registry",
                   b"hcd_registered",
                   b"hcd_ep0_failures",
                   b"hcd_port_reset_settle_ms",
                   b"probe_summary",
                   b"clk_sys_hz",
                   b"tx_dma_channel"):
        if needle not in airliftctl:
            raise SystemExit(f"airliftctl missing marker {needle!r}")

    ftpd = read_bytes(tf, "./usr/sbin/fruitjam-ftpd")
    for needle in (b"EPSV", b"EPRT", b"PORT", b"APPE", b"RNFR"):
        if needle not in ftpd:
            raise SystemExit(f"fruitjam-ftpd missing marker {needle!r}")

    shell = read_bytes(tf, "./usr/bin/fruitjam-shell")
    for needle in (b"history", b"tab command/path completion", b"simple commands"):
        if needle not in shell:
            raise SystemExit(f"fruitjam-shell missing marker {needle!r}")

    telnetd = read_bytes(tf, "./usr/sbin/fruitjam-telnetd")
    if len(telnetd) < 4096 or not telnetd.startswith(b"bFLT"):
        raise SystemExit("fruitjam-telnetd is not a valid nonempty bFLT executable")

dt_props = parse_fdt(dtb.read_bytes())
require_u32s(dt_props, "/clk-sys", "clock-frequency", [252000000])
require_u32s(dt_props, "/fruitjam-pins", "adafruit,usb-host-gpios", [1, 2, 11])
require_u32s(dt_props, "/fruitjam-pins", "adafruit,i2c-gpios", [20, 21])
require_u32s(dt_props, "/fruitjam-pins", "adafruit,i2s-gpios", [24, 25, 26, 27, 23])
require_u32s(dt_props, "/fruitjam-pins", "adafruit,dvi-gpios", [12, 13, 14, 15, 16, 17, 18, 19])
i2c_sda = prop_u32s(dt_props, "/i2c-gpio0", "sda-gpios")
i2c_scl = prop_u32s(dt_props, "/i2c-gpio0", "scl-gpios")
if len(i2c_sda) != 3 or i2c_sda[1:] != [20, 6]:
    raise SystemExit(f"DTB /i2c-gpio0 sda-gpios = {i2c_sda}, expected phandle 20 6")
if len(i2c_scl) != 3 or i2c_scl[1:] != [21, 6]:
    raise SystemExit(f"DTB /i2c-gpio0 scl-gpios = {i2c_scl}, expected phandle 21 6")
require_string(dt_props, "/i2c-gpio0", "status", "okay")
usbhost_path = "/soc/usbhost-bridge@d0000000"
if "adafruit,fruit-jam-rp2350-usbhost" not in prop_strings(dt_props, usbhost_path, "compatible"):
    raise SystemExit("DTB usbhost bridge compatible does not include adafruit,fruit-jam-rp2350-usbhost")
for name, expected in (
    ("raspberrypi,dp-gpio", [1]),
    ("raspberrypi,dm-gpio", [2]),
    ("raspberrypi,power-gpio", [11]),
    ("raspberrypi,pio", [0]),
    ("raspberrypi,sm-tx", [0]),
    ("raspberrypi,sm-rx", [1]),
    ("raspberrypi,sm-eop", [2]),
    ("raspberrypi,tx-dma-channel", [9]),
    ("raspberrypi,clk-sys-hz", [252000000]),
    ("raspberrypi,hcd-start-delay-ms", [8000]),
    ("raspberrypi,hcd-port-reset-settle-ms", [500]),
    ("raspberrypi,hcd-port-reset-sof-frames", [25]),
):
    require_u32s(dt_props, usbhost_path, name, expected)
if "raspberrypi,hcd-manual-start" not in dt_props.get(usbhost_path, {}):
    raise SystemExit("DTB usbhost bridge must set raspberrypi,hcd-manual-start")
require_string(dt_props, usbhost_path, "status", "okay")
audio_path = "/soc/audio-clock@50300000"
if prop_strings(dt_props, audio_path, "compatible") != ["adafruit,fruit-jam-rp2350-audio-clock"]:
    raise SystemExit("DTB audio clock compatible mismatch")
for name, expected in (
    ("raspberrypi,pio", [1]),
    ("raspberrypi,sm", [1]),
    ("raspberrypi,mclk-sm", [0]),
    ("raspberrypi,din-gpio", [24]),
    ("raspberrypi,mclk-gpio", [25]),
    ("raspberrypi,bclk-gpio", [26]),
    ("raspberrypi,ws-gpio", [27]),
    ("raspberrypi,gpio-base", [16]),
    ("raspberrypi,clk-sys-hz", [252000000]),
    ("raspberrypi,mclk-hz", [15000000]),
):
    require_u32s(dt_props, audio_path, name, expected)
require_string(dt_props, audio_path, "status", "okay")
dvi_path = "/soc/dvi@400c0000"
if prop_strings(dt_props, dvi_path, "compatible") != ["adafruit,fruit-jam-rp2350-dvi"]:
    raise SystemExit("DTB DVI compatible mismatch")
if prop_strings(dt_props, dvi_path, "reg-names") != [
    "hstx-ctrl", "hstx-fifo", "dma", "resets",
    "io-bank0", "pads-bank0", "busctrl", "clocks",
]:
    raise SystemExit("DTB DVI reg-names mismatch")
require_u32s(dt_props, dvi_path, "interrupts", [10])
require_string(dt_props, dvi_path, "status", "okay")

image = kernel_image.read_bytes()
for needle in (
    b"fruitjam-usbhost",
    b"fruitjam-audio",
    b"Fruit Jam audio clock ready on /dev/fruitjam-audio",
    b"last_waveform",
    b"wave %15s %u %u",
    b"tone %u %u %15s",
    b"fruitjam-dvi",
    b"Fruit Jam HSTX DVI registered at %ux%u RGB332, idle",
    b"wili-show",
    b"wili-test",
    b"usbhid: USB HID core driver",
    b"hid-generic",
    b"xpad_irq_in",
    b"xpad_irq_out",
    b"Xbox 360 Wireless Receiver (XBOX)",
    b"hcd_manual_start %u",
    b"raspberrypi,hcd-manual-start",
    b"hcd_port_reset_settle_ms %u",
    b"hcd_port_reset_sof_frames %u",
    b"hcd_data_ack_tail_drain_us %u",
    b"raspberrypi,hcd-port-reset-settle-ms",
    b"raspberrypi,hcd-port-reset-sof-frames",
    b"hcd_reset_settle_ms=%u",
    b"USB host HCD waiting for hcd-start",
    b"hcd-start",
    b"hcd-port-reset-settle-ms=%u",
    b"sof-frames=%u",
    b"status-busy lock-held",
    b"hcd-prestart-power-cycle off-ms=%u on-ms=%u reset-ms=%u",
    b"USB host HCD pre-start power cycle complete",
    b"scheduling automatic recovery",
    b"hcd-auto-recover %u done",
    b"auto recovery limit reached",
    b"hcd-fault auto-recovery-limit=%u",
    b"hcd-fault ep0-failures=%u last-ret=%d pid=0x%02x rx-len=%u prev=%s",
):
    if needle not in image:
        raise SystemExit(f"kernel Image missing USB host/input marker {needle!r}")

boot = bootloader.read_bytes()
for needle in (
    b"Linux rescue watchdog fired; entering BOOTSEL.",
    b"BOOTSEL watchdog vector reached bootloader; entering BOOTSEL.",
):
    if needle not in boot:
        raise SystemExit(f"bootloader.bin missing recovery marker {needle!r}")

sha256 = hashlib.sha256(uf2.read_bytes()).hexdigest()
print(f"ok rootfs {rootfs}")
print(f"ok dtb {dtb}")
print(f"ok kernel Image {kernel_image}")
print(f"ok bootloader {bootloader}")
print(f"ok uf2 {uf2} sha256={sha256}")
PY
