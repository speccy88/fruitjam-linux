#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image_dir=${1:-"$repo/buildroot-output-docker-images"}
rootfs="$image_dir/rootfs.tar"
uf2="$image_dir/flash-image.uf2"

if [ ! -s "$rootfs" ]; then
	echo "validate-fruitjam-image: missing $rootfs" >&2
	exit 1
fi
if [ ! -s "$uf2" ]; then
	echo "validate-fruitjam-image: missing $uf2" >&2
	exit 1
fi

python3 - "$rootfs" "$uf2" <<'PY'
import hashlib
import sys
import tarfile
from pathlib import Path

rootfs = Path(sys.argv[1])
uf2 = Path(sys.argv[2])

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
    "./usr/bin/fruitjam-ps",
    "./usr/bin/ps",
    "./usr/bin/fruitjam-pgrep",
    "./usr/bin/pgrep",
    "./usr/bin/pkill",
    "./usr/bin/mosquitto_pub",
    "./usr/bin/mosquitto_sub",
    "./usr/bin/wget",
    "./usr/sbin/fruitjam-ftpd",
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

    fruitjam_pgrep = read_bytes(tf, "./usr/bin/fruitjam-pgrep")
    for needle in (b"fruitjam-pgrep", b"pgrep", b"pkill", b"PATTERN"):
        if needle not in fruitjam_pgrep:
            raise SystemExit(f"fruitjam-pgrep missing marker {needle!r}")

    airliftctl = read_bytes(tf, "./usr/bin/airliftctl")
    for needle in (b"mqtt-sub", b"mqtt-pub", b"USERNAME PASSWORD"):
        if needle not in airliftctl:
            raise SystemExit(f"airliftctl missing marker {needle!r}")

    ftpd = read_bytes(tf, "./usr/sbin/fruitjam-ftpd")
    for needle in (b"EPSV", b"EPRT", b"PORT", b"APPE", b"RNFR"):
        if needle not in ftpd:
            raise SystemExit(f"fruitjam-ftpd missing marker {needle!r}")

    telnetd = read_bytes(tf, "./usr/sbin/fruitjam-telnetd")
    if len(telnetd) < 4096 or not telnetd.startswith(b"bFLT"):
        raise SystemExit("fruitjam-telnetd is not a valid nonempty bFLT executable")

sha256 = hashlib.sha256(uf2.read_bytes()).hexdigest()
print(f"ok rootfs {rootfs}")
print(f"ok uf2 {uf2} sha256={sha256}")
PY
