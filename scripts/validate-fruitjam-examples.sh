#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
berry_src="$repo/board/adafruit/adafruit_fruit_jam_rp2350/rootfs_overlay/root/berry"
shell_src="$repo/board/adafruit/adafruit_fruit_jam_rp2350/rootfs_overlay/root/sh"
rtttl_src="$repo/board/adafruit/adafruit_fruit_jam_rp2350/rootfs_overlay/root/rtttl"
host_berry="$repo/buildroot/dl/berry/git/berry"
web_cgi_src="$repo/package/fruitjam-utils/src/fruitjam-web-cgi.c"
web_dispatch_cgi_src="$repo/package/fruitjam-utils/src/fruitjam-web-dispatch-cgi.c"
berry_json_src="$repo/package/fruitjam-utils/src/fruitjam-berry-json.c"
web_page_src="$repo/board/adafruit/adafruit_fruit_jam_rp2350/rootfs_overlay/www/index.html"
dvi_src="$repo/package/fruitjam-utils/src/fruitjam-dvi.c"
rtttl_src_c="$repo/package/fruitjam-utils/src/fruitjam-rtttl.c"
wavplay_src="$repo/package/fruitjam-utils/src/fruitjam-wavplay.c"
telnetd_src="$repo/package/fruitjam-utils/src/fruitjam-telnetd.c"
wget_src="$repo/package/fruitjam-utils/src/fruitjam-wget.c"
httpd_src="$repo/package/fruitjam-utils/src/fruitjam-httpd.c"
services_src="$repo/package/fruitjam-utils/src/fruitjam-services.c"
buttons_src="$repo/package/fruitjam-utils/src/fruitjam-buttons.c"
airlift_src="$repo/package/fruitjam-airlift/src/airliftctl.c"
usbhost_src="$repo/package/fruitjam-utils/src/fruitjam-usbhost.c"
hidkeys_src="$repo/package/fruitjam-utils/src/fruitjam-hidkeys.c"
mosquitto_pub_src="$repo/package/fruitjam-utils/src/mosquitto_pub.c"
mosquitto_sub_src="$repo/package/fruitjam-utils/src/mosquitto_sub.c"
uart_login_src="$repo/package/fruitjam-utils/src/fruitjam-uart-login.c"
mem_src="$repo/package/fruitjam-utils/src/fruitjam-mem.c"
cdc_smoke_src="$repo/scripts/cdc-smoke-test.py"
bootloader_clocks_src="$repo/package/pico2-bootloader/bootloader/src/clocks.h"
kernel_usbhost_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0020-misc-add-fruitjam-usbhost-bridge-driver.patch"
kernel_usbhost_pio_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0021-misc-stage-fruitjam-usbhost-pio2-engine.patch"
kernel_usbhost_tx_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0022-misc-configure-fruitjam-usbhost-pio2-tx-path.patch"
kernel_usbhost_rx_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0023-misc-add-fruitjam-usbhost-pio2-rx-probes.patch"
kernel_usbhost_dma_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0024-misc-use-dma-for-fruitjam-usbhost-pio2-tx.patch"
kernel_usbhost_reset_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0025-misc-reset-fruitjam-usbhost-bus-with-pio-overrides.patch"
kernel_usbhost_reloc_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0026-misc-relocate-fruitjam-usbhost-pio-jump-targets.patch"
kernel_usbhost_rx_osr_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0027-misc-initialize-fruitjam-usbhost-rx-osr.patch"
kernel_usbhost_selfrx_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0028-misc-add-fruitjam-usbhost-self-rx-probe.patch"
kernel_usbhost_eop_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0029-misc-initialize-fruitjam-usbhost-eop-sm-pc.patch"
kernel_usbhost_eop_reset_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0030-misc-reset-fruitjam-usbhost-eop-before-rx.patch"
kernel_usbhost_tx_latch_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0031-misc-initialize-fruitjam-usbhost-tx-idle-latch.patch"
kernel_usbhost_tx_idle_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0032-misc-reset-fruitjam-usbhost-tx-sm-to-idle-release.patch"
kernel_usbhost_tx_eop_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0033-misc-wait-for-fruitjam-usbhost-tx-eop.patch"
kernel_usbhost_debug_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0034-misc-expose-fruitjam-usbhost-pio-debug-state.patch"
kernel_usbhost_debug_finish_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0035-misc-complete-fruitjam-usbhost-debug-status.patch"
kernel_usbhost_gated_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0036-misc-add-fruitjam-usbhost-gated-rx-probes.patch"
kernel_usbhost_gated_write_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0037-misc-expose-fruitjam-usbhost-gated-rx-commands.patch"
kernel_usbhost_dma_eop_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0038-misc-preserve-fruitjam-usbhost-tx-eop-after-dma-start.patch"
kernel_usbhost_dma_idle_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0039-misc-accept-fruitjam-usbhost-dma-tx-idle-completion.patch"
kernel_usbhost_rx_drain_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0040-misc-drain-fruitjam-usbhost-rx-fifo-after-eop.patch"
kernel_usbhost_setup_selfrx_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0041-misc-add-fruitjam-usbhost-setup-data-self-rx.patch"
kernel_usbhost_rx_tail_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0042-misc-drain-fruitjam-usbhost-rx-tail-after-eop.patch"
kernel_usbhost_cpu_tx_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0043-misc-add-fruitjam-usbhost-cpu-tx-probes.patch"
kernel_usbhost_noeop_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0044-misc-add-fruitjam-usbhost-noeop-self-rx-probe.patch"
kernel_usbhost_sweep_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0045-misc-add-fruitjam-usbhost-data-length-sweep.patch"
kernel_usbhost_empty_eop_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0046-misc-ignore-empty-fruitjam-usbhost-rx-eop.patch"
kernel_usbhost_clock_diag_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0047-misc-expose-fruitjam-usbhost-clock-diagnostics.patch"
kernel_usbhost_active_sof_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0048-misc-add-fruitjam-usbhost-active-sof-probes.patch"
kernel_usbhost_combo_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0049-misc-add-fruitjam-usbhost-combined-setup-probe.patch"
kernel_usbhost_fast_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0050-misc-add-fruitjam-usbhost-fast-setup-probe.patch"
kernel_usbhost_tight_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0054-misc-add-fruitjam-usbhost-tight-setup-probe.patch"
kernel_usbhost_burst_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0051-misc-add-fruitjam-usbhost-burst-setup-probe.patch"
kernel_usbhost_stream_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0052-misc-add-fruitjam-usbhost-streamed-setup-probe.patch"
kernel_usbhost_stream_wait_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0053-misc-wait-for-fruitjam-usbhost-streamed-tx-idle.patch"
kernel_usbhost_live_drain_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0055-misc-add-fruitjam-usbhost-live-rx-drain-probe.patch"
kernel_usbhost_low_speed_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0056-misc-make-fruitjam-usbhost-low-speed-aware.patch"
kernel_usbhost_tx_eop_gated_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0057-misc-preserve-fruitjam-usbhost-tx-eop-for-gated-rx.patch"
kernel_usbhost_combo_skipack_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0058-misc-add-fruitjam-usbhost-combo-skipack-probe.patch"
kernel_usbhost_keyboard_target_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0060-misc-parameterize-fruitjam-usbhost-keyboard-target.patch"
kernel_config_src="$repo/board/adafruit/adafruit_fruit_jam_rp2350/adafruit_fruit_jam_rp2350.config"
dts_src="$repo/board/adafruit/adafruit_fruit_jam_rp2350/dts/sifive/adafruit_fruit_jam_rp2350.dts"
inittab_src="$repo/board/adafruit/adafruit_fruit_jam_rp2350/rootfs_overlay/etc/inittab"
profile_src="$repo/board/adafruit/adafruit_fruit_jam_rp2350/rootfs_overlay/etc/profile"
cls_src="$repo/board/adafruit/adafruit_fruit_jam_rp2350/rootfs_overlay/etc/profile.d/cls.sh"

if [ ! -x "$host_berry" ]; then
	echo "validate-fruitjam-examples: missing host Berry at $host_berry" >&2
	exit 1
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fruitjam-examples.XXXXXX")
trap 'test -z "${httpd_pid:-}" || kill "$httpd_pid" 2>/dev/null || true; test -z "${uart_pid:-}" || kill "$uart_pid" 2>/dev/null || true; rm -rf "$tmp"' EXIT HUP INT TERM

cp -R "$berry_src" "$tmp/berry"
user_berry_dir="$tmp/user-berry"
mkdir -p "$user_berry_dir"
cat > "$user_berry_dir/user-http.be" <<'EOF'
print("USER_HTTP_BERRY_OK")
EOF
cat > "$user_berry_dir/not-berry.txt" <<'EOF'
not a berry script
EOF

rewrite_berry_dir() {
	file=$1
	awk -v repl="var BERRY_DIR = \"$tmp/berry\"" '
		/^var BERRY_DIR = / { print repl; next }
		{ print }
	' "$file" > "$file.tmp"
	mv "$file.tmp" "$file"
}

rewrite_berry_dir "$tmp/berry/run-all.be"
rewrite_berry_dir "$tmp/berry/run-visual.be"

fakefs="$tmp/berry-fakefs"
mkdir -p "$fakefs/gpio" "$fakefs/adc"
touch "$fakefs/neopixels" "$fakefs/fruitjam-audio" "$fakefs/fruitjam-dvi" \
	"$fakefs/i2c-0" "$fakefs/spidev0.0"
for spec in 0:1 4:0 5:1 11:1 1:1 2:0; do
	gpio=${spec%%:*}
	value=${spec#*:}
	mkdir -p "$fakefs/gpio/gpio$gpio"
	printf '%s\n' "$value" > "$fakefs/gpio/gpio$gpio/value"
	: > "$fakefs/gpio/gpio$gpio/direction"
done
for spec in 0:123:330 1:456:1650 8:789:250; do
	channel=${spec%%:*}
	rest=${spec#*:}
	raw=${rest%%:*}
	mv=${rest#*:}
	printf '%s\n' "$raw" > "$fakefs/adc/raw$channel"
	printf '%s\n' "$mv" > "$fakefs/adc/millivolts$channel"
done
awk -v repl="var FAKE_ROOT = \"$fakefs\"" '
	/^var FAKE_ROOT = / { print repl; next }
	{ print }
' "$tmp/berry/07-fruitjam-module-fakefs.be" > "$tmp/berry/07-fruitjam-module-fakefs.be.tmp"
mv "$tmp/berry/07-fruitjam-module-fakefs.be.tmp" "$tmp/berry/07-fruitjam-module-fakefs.be"

echo "== berry whitelist sync =="
python3 - "$berry_src" "$web_cgi_src" "$web_dispatch_cgi_src" "$berry_json_src" "$airlift_src" <<'PY'
import re
import sys
from pathlib import Path

berry_dir = Path(sys.argv[1])
sources = [Path(p) for p in sys.argv[2:]]

def script_list(path: Path) -> list[str]:
    text = path.read_text()
    match = re.search(
        r"static const char \*const berry_scripts\[\] = \{(?P<body>.*?)\};",
        text,
        re.S,
    )
    if not match:
        raise SystemExit(f"{path}: missing berry_scripts list")
    return re.findall(r'"([^"]+\.be)"', match.group("body"))

lists = {path: script_list(path) for path in sources}
first_path = sources[0]
first = lists[first_path]
if len(first) != len(set(first)):
    raise SystemExit(f"{first_path}: duplicate Berry whitelist entries")
for path, entries in lists.items():
    if entries != first:
        raise SystemExit(f"{path}: Berry whitelist differs from {first_path}")
for name in first:
    script = berry_dir / name
    if not script.is_file():
        raise SystemExit(f"missing whitelisted Berry script: {script}")
print("ok berry whitelist", len(first), "scripts")
PY

echo "== web action sync =="
python3 - "$web_page_src" "$web_cgi_src" <<'PY'
import re
import sys
from pathlib import Path

web_path = Path(sys.argv[1])
cgi_path = Path(sys.argv[2])
web = web_path.read_text()
cgi = cgi_path.read_text()

web_actions = set(re.findall(r'action:\s*"([^"]+)"', web))
cgi_actions = set(re.findall(r'!strcmp\(action,\s*"([^"]+)"\)', cgi))
web_dvi_cmds = set(re.findall(r'data-dvi="([^"]+)"', web))
web_usbhost_cmds = set(re.findall(r'data-usbhost="([^"]+)"', web))
dvi_allowed_match = re.search(
    r'static const char \*const allowed\[\] = \{(?P<body>.*?)\};',
    cgi,
    re.S,
)
if not dvi_allowed_match:
    raise SystemExit("CGI missing DVI allowed command table")
cgi_dvi_cmds = set(re.findall(r'"([^"]+)"', dvi_allowed_match.group("body")))
cgi_dvi_cmds.update({"dashboard", "text"})
cgi_usbhost_cmds = {
    "status",
    "on",
    "off",
    "reset",
    "in-token",
    "get-device-8",
    "get-device-8-combo-skipack",
    "reset-get-device-8-combo-skipack",
    "kbd-find",
    "kbd-auto-text",
    "kbd-auto-events",
    "kbd-auto-shell",
}
required = {
    "status",
    "neopixels",
    "rtttl",
    "wav-list",
    "wav-play",
    "dvi",
    "usbhost",
    "berry-list",
    "berry-run",
    "i2c",
    "adc",
    "button-test",
    "bootsel",
}
missing_web = sorted(required - web_actions)
if missing_web:
    raise SystemExit(f"web page missing required actions: {missing_web}")
missing_cgi = sorted(web_actions - cgi_actions)
if missing_cgi:
    raise SystemExit(f"web page calls actions not handled by CGI: {missing_cgi}")
missing_required_cgi = sorted(required - cgi_actions)
if missing_required_cgi:
    raise SystemExit(f"CGI missing required actions: {missing_required_cgi}")
bad_dvi = sorted(web_dvi_cmds - cgi_dvi_cmds)
if bad_dvi:
    raise SystemExit(f"web page has unsupported DVI commands: {bad_dvi}")
bad_usbhost = sorted(web_usbhost_cmds - cgi_usbhost_cmds)
if bad_usbhost:
    raise SystemExit(f"web page has unsupported USB-host commands: {bad_usbhost}")
for required_dvi in ("dashboard", "text"):
    if required_dvi not in web_dvi_cmds:
        raise SystemExit(f"web page missing DVI command: {required_dvi}")
for required_usbhost in (
    "status",
    "on",
    "off",
    "reset",
    "in-token",
    "get-device-8",
    "get-device-8-combo-skipack",
    "reset-get-device-8-combo-skipack",
    "kbd-find",
    "kbd-auto-text",
    "kbd-auto-events",
    "kbd-auto-shell",
):
    if required_usbhost not in web_usbhost_cmds:
        raise SystemExit(f"web page missing USB-host command: {required_usbhost}")
print(
    "ok web actions",
    len(web_actions),
    "actions",
    len(web_dvi_cmds),
    "dvi commands",
    len(web_usbhost_cmds),
    "usbhost commands",
)
PY

check_cgi_json() {
	query=$1
	out="$tmp/cgi.out"

	QUERY_STRING="$query" "$tmp/fruitjam-web.cgi" > "$out"
	python3 - "$out" "$query" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
query = sys.argv[2]
raw = path.read_text()
header, sep, body = raw.partition("\n\n")
if not sep:
    raise SystemExit(f"{query}: missing CGI header separator")
if "Content-Type: application/json" not in header:
    raise SystemExit(f"{query}: missing JSON content type")
data = json.loads(body)
if "ok" not in data:
    raise SystemExit(f"{query}: JSON response missing ok")
if query == "action=status":
    if data.get("source") == "tiny-berry-json":
        raise SystemExit("status unexpectedly used Berry helper")
    control = data.get("control") or {}
    if control.get("mode") != "direct-cgi":
        raise SystemExit("status did not report direct-cgi mode")
if query == "action=berry-list":
    scripts = data.get("scripts") or []
    for required in ("00-hello.be", "run-all.be", "neopixel-rainbow-10s.be"):
        if required not in scripts:
            raise SystemExit(f"berry-list missing {required}")
    if "user:user-http.be" not in scripts:
        raise SystemExit("berry-list missing SD-card user script reference")
    if "user:not-berry.txt" in scripts or "not-berry.txt" in (data.get("user_scripts") or []):
        raise SystemExit("berry-list exposed non-Berry SD file")
    if "user-http.be" not in (data.get("user_scripts") or []):
        raise SystemExit("berry-list missing user_scripts entry")
if query == "action=usbhost":
    if data.get("hid") is not False or data.get("driver") not in ("sysfs-line-state", "kernel-line-state"):
        raise SystemExit("usbhost API must not claim HID stack support yet")
    if data.get("next") != "pio-packet-io":
        raise SystemExit("usbhost API missing PIO packet I/O bridge hint")
    if data.get("first_milestone") != "boot-protocol-keyboard":
        raise SystemExit("usbhost API missing boot keyboard milestone")
    for key in ("pio_configured", "packets", "tx_errors", "last_tx_result",
                "last_tx_len", "rx_attempts", "rx_errors",
                "last_rx_result", "last_rx_len", "last_rx_pid",
                "last_rx_hex", "probe_ok"):
        if key not in data:
            raise SystemExit(f"usbhost API missing {key}")
if query == "action=berry-run&script=../../bad.be" and data.get("ok") is not False:
    raise SystemExit("bad berry-run path was not rejected")
PY
	echo "ok cgi $query"
}

echo "== web cgi host JSON =="
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-DBERRY_USER_DIR="\"$user_berry_dir\"" \
	-o "$tmp/fruitjam-web.cgi" "$web_cgi_src"
check_cgi_json "action=status"
check_cgi_json "action=i2c"
check_cgi_json "action=usbhost"
check_cgi_json "action=berry-list"
check_cgi_json "action=wav-list"
check_cgi_json "action=neopixels"
check_cgi_json "action=berry-run&script=../../bad.be"

cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-DBERRY_USER_DIR="\"$user_berry_dir\"" \
	-o "$tmp/fruitjam-web-dispatch.cgi" "$web_dispatch_cgi_src"
QUERY_STRING="action=berry-list" "$tmp/fruitjam-web-dispatch.cgi" > "$tmp/fruitjam-web-dispatch.out"
python3 - "$tmp/fruitjam-web-dispatch.out" <<'PY'
import json
import sys
from pathlib import Path

raw = Path(sys.argv[1]).read_text()
header, sep, body = raw.partition("\n\n")
if not sep or "Content-Type: application/json" not in header:
    raise SystemExit("dispatch CGI berry-list missing JSON header")
data = json.loads(body)
scripts = data.get("scripts") or []
if data.get("source") != "direct-cgi-tiny":
    raise SystemExit("dispatch CGI source marker changed")
if "00-hello.be" not in scripts or "user:user-http.be" not in scripts:
    raise SystemExit(f"dispatch CGI berry-list missing scripts: {scripts}")
if "user-http.be" not in (data.get("user_scripts") or []):
    raise SystemExit("dispatch CGI missing user_scripts entry")
PY
echo "ok cgi dispatch berry-list"

echo "== berry JSON helper host behavior =="
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-DBERRY_BIN="\"$host_berry\"" \
	-DBERRY_DIR="\"$tmp/berry\"" \
	-DBERRY_USER_DIR="\"$user_berry_dir\"" \
	-o "$tmp/fruitjam-berry-json" "$berry_json_src"
"$tmp/fruitjam-berry-json" 00-hello.be > "$tmp/berry-json-example.out"
"$tmp/fruitjam-berry-json" user:user-http.be > "$tmp/berry-json-user.out"
"$tmp/fruitjam-berry-json" user:not-berry.txt > "$tmp/berry-json-bad.out"
python3 - "$tmp/berry-json-example.out" "$tmp/berry-json-user.out" "$tmp/berry-json-bad.out" <<'PY'
import json
import sys
from pathlib import Path

example = json.loads(Path(sys.argv[1]).read_text())
user = json.loads(Path(sys.argv[2]).read_text())
bad = json.loads(Path(sys.argv[3]).read_text())
if not example.get("ok") or example.get("script_source") != "example":
    raise SystemExit(f"built-in Berry JSON run failed: {example}")
if "Fruit Jam Berry hello" not in example.get("output", ""):
    raise SystemExit("built-in Berry JSON output missing hello text")
if not user.get("ok") or user.get("script_source") != "user":
    raise SystemExit(f"user Berry JSON run failed: {user}")
if "USER_HTTP_BERRY_OK" not in user.get("output", ""):
    raise SystemExit("user Berry JSON output missing marker")
if bad.get("ok") is not False:
    raise SystemExit("Berry JSON helper accepted invalid user script")
PY
echo "ok berry JSON helper"

echo "== loopback http route host test =="
sd_www="$tmp/sd-www"
playground_www="$tmp/playground-www"
mkdir -p "$sd_www" "$playground_www"
printf 'USER_SD_INDEX\n' > "$sd_www/index.html"
printf 'PLAYGROUND_INDEX\n' > "$playground_www/index.html"
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-DPLAYGROUND_ROOT="\"$playground_www\"" \
	-DSD_WEB_ROOT="\"$sd_www\"" \
	-o "$tmp/fruitjam-httpd" "$httpd_src"
port=$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)
"$tmp/fruitjam-httpd" "$port" > "$tmp/fruitjam-httpd.log" 2>&1 &
httpd_pid=$!
python3 - "$port" <<'PY'
import sys
import time
import urllib.request

port = int(sys.argv[1])
base = f"http://127.0.0.1:{port}"
deadline = time.time() + 5
last_error = None
while time.time() < deadline:
    try:
        root = urllib.request.urlopen(base + "/", timeout=1).read().decode()
        playground = urllib.request.urlopen(base + "/playground", timeout=1).read().decode()
        playground_slash = urllib.request.urlopen(base + "/playground/", timeout=1).read().decode()
        if "USER_SD_INDEX" not in root:
            raise SystemExit("root did not serve SD index")
        if "PLAYGROUND_INDEX" not in playground or "PLAYGROUND_INDEX" not in playground_slash:
            raise SystemExit("/playground did not serve built-in playground")
        break
    except Exception as exc:
        last_error = exc
        time.sleep(0.1)
else:
    raise SystemExit(f"fruitjam-httpd route test failed: {last_error}")
PY
kill "$httpd_pid"
wait "$httpd_pid" 2>/dev/null || true
httpd_pid=
echo "ok loopback http routes"

echo "== uart login host behavior =="
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-DLOGIN_SHELL=\"/usr/bin/true\" \
	-o "$tmp/fruitjam-uart-login" "$uart_login_src"
"$tmp/fruitjam-uart-login" < /dev/null > "$tmp/uart-login-eof.out" 2>&1 &
uart_pid=$!
sleep 1
if ! kill -0 "$uart_pid" 2>/dev/null; then
	cat "$tmp/uart-login-eof.out" >&2
	echo "fruitjam-uart-login exited on EOF" >&2
	exit 1
fi
kill "$uart_pid" 2>/dev/null || true
wait "$uart_pid" 2>/dev/null || true
uart_pid=
printf '\n' | "$tmp/fruitjam-uart-login" > "$tmp/uart-login-enter.out" 2>&1
echo "ok uart login eof wait"
echo "ok uart login enter exec"

echo "== telnet shell host line editing =="
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-o "$tmp/fruitjam-shell" "$repo/package/fruitjam-utils/src/fruitjam-shell.c"
python3 - "$tmp/fruitjam-shell" <<'PY'
import errno
import fcntl
import os
import pty
import subprocess
import sys
import time

exe = sys.argv[1]
master, slave = pty.openpty()
proc = subprocess.Popen([exe], stdin=slave, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)
flags = fcntl.fcntl(master, fcntl.F_GETFL)
fcntl.fcntl(master, fcntl.F_SETFL, flags | os.O_NONBLOCK)
out = bytearray()

def read_for(seconds):
    end = time.time() + seconds
    while time.time() < end:
        try:
            chunk = os.read(master, 4096)
            if chunk:
                out.extend(chunk)
                end = time.time() + 0.15
        except BlockingIOError:
            pass
        except OSError as exc:
            if exc.errno != errno.EIO:
                raise
            break
        time.sleep(0.02)

read_for(0.5)
os.write(master, b"ec\tTAB_OK\r")
read_for(0.5)
os.write(master, b"echo HIST_OK\r")
read_for(0.5)
os.write(master, b"\x1b[A\r")
read_for(0.5)
os.write(master, b"history\r")
read_for(0.5)
os.write(master, b"exit\r")
read_for(0.5)
proc.wait(timeout=2)
text = out.decode("utf-8", "replace")
if "echo TAB_OK" not in text or "TAB_OK" not in text:
    raise SystemExit("fruitjam-shell tab completion failed")
if text.count("HIST_OK") < 2:
    raise SystemExit("fruitjam-shell history recall failed")
if "history" not in text or "echo HIST_OK" not in text:
    raise SystemExit("fruitjam-shell history builtin failed")
print("ok fruitjam-shell line editing")
PY

echo "== memory helper host behavior =="
proc_root="$tmp/proc"
mkdir -p "$proc_root"
cat > "$proc_root/meminfo" <<'EOF'
MemTotal:        8192 kB
MemFree:         2048 kB
MemAvailable:   4096 kB
Buffers:          256 kB
Cached:          1024 kB
SReclaimable:     128 kB
Shmem:             64 kB
SwapTotal:          0 kB
SwapFree:           0 kB
CommitLimit:     8192 kB
Committed_AS:    2048 kB
EOF
printf '12.34 56.78\n' > "$proc_root/uptime"
printf '0.01 0.02 0.03 1/23 456\n' > "$proc_root/loadavg"
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-DPROC_ROOT="\"$proc_root\"" \
	-o "$tmp/fruitjam-mem" "$mem_src"
"$tmp/fruitjam-mem" > "$tmp/fruitjam-mem.txt"
"$tmp/fruitjam-mem" json > "$tmp/fruitjam-mem.json"
python3 - "$tmp/fruitjam-mem.txt" "$tmp/fruitjam-mem.json" <<'PY'
import json
import sys
from pathlib import Path

text = Path(sys.argv[1]).read_text()
data = json.loads(Path(sys.argv[2]).read_text())
if "Fruit Jam memory" not in text or "Note: no-MMU" not in text:
    raise SystemExit("fruitjam-mem text output missing expected labels")
if data.get("unit") != "kB" or data.get("mem", {}).get("total") != 8192:
    raise SystemExit("fruitjam-mem json missing memory totals")
if data.get("mem", {}).get("cache") != 1088:
    raise SystemExit("fruitjam-mem json cache calculation regressed")
if data.get("uptime_seconds") != 12.34 or data.get("load_15m") != 0.03:
    raise SystemExit("fruitjam-mem json uptime/load parse regressed")
PY
echo "ok fruitjam-mem text"
echo "ok fruitjam-mem json"

echo "== usbhost helper host behavior =="
gpio_root="$tmp/gpio"
mkdir -p "$gpio_root"
: > "$gpio_root/export"
for spec in 11:1 1:1 2:0; do
	gpio=${spec%%:*}
	value=${spec#*:}
	mkdir -p "$gpio_root/gpio$gpio"
	printf '%s\n' "$value" > "$gpio_root/gpio$gpio/value"
	: > "$gpio_root/gpio$gpio/direction"
done
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-DGPIO_ROOT="\"$gpio_root\"" \
	-o "$tmp/fruitjam-usbhost" "$usbhost_src"
"$tmp/fruitjam-usbhost" status > "$tmp/fruitjam-usbhost.txt"
"$tmp/fruitjam-usbhost" json > "$tmp/fruitjam-usbhost.json"
"$tmp/fruitjam-usbhost" wait 0 > "$tmp/fruitjam-usbhost-wait.txt"
"$tmp/fruitjam-usbhost" monitor 0 > "$tmp/fruitjam-usbhost-monitor.txt"
"$tmp/fruitjam-usbhost" reset 10 > "$tmp/fruitjam-usbhost-reset.txt"
"$tmp/fruitjam-usbhost" decode c312010002000000403412 > "$tmp/fruitjam-usbhost-decode-direct.txt"
"$tmp/fruitjam-usbhost" decode f04b12010002000000403412 > "$tmp/fruitjam-usbhost-decode-bridge.txt"
"$tmp/fruitjam-usbhost" decode f04b0000040000000000abcd > "$tmp/fruitjam-usbhost-decode-hid.txt"
"$tmp/fruitjam-usbhost" hid f04b0000040000000000abcd > "$tmp/fruitjam-usbhost-hid-packet.txt"
"$tmp/fruitjam-usbhost" hid 0200050000000000 > "$tmp/fruitjam-usbhost-hid-raw.txt"
if "$tmp/fruitjam-usbhost" hid f04b12010002000000403412 > "$tmp/fruitjam-usbhost-hid-descriptor.txt" 2>&1; then
	echo "fruitjam-usbhost hid accepted a descriptor packet as keyboard input" >&2
	exit 1
fi
python3 - "$usbhost_src" "$tmp" <<'PY'
import errno
import fcntl
import os
import pty
import subprocess
import sys
import termios
import time

src, tmp = sys.argv[1], sys.argv[2]
def status_for_report(report_hex):
    return (
        b"power 1\n"
        b"dp 1\n"
        b"dm 0\n"
        b"pio_ready 1\n"
        b"pio_configured 1\n"
        b"packets 1\n"
        b"tx_errors 0\n"
        b"last_tx_result 0\n"
        b"last_tx_len 5\n"
        b"rx_attempts 1\n"
        b"rx_errors 0\n"
        b"last_rx_result 0\n"
        b"last_rx_len 12\n"
        b"last_rx_pid 0x4b\n"
        + f"last_rx_hex f04b{report_hex}abcd\n".encode()
    )

def status_for_pid(pid_hex):
    return (
        b"power 1\n"
        b"dp 1\n"
        b"dm 0\n"
        b"pio_ready 1\n"
        b"pio_configured 1\n"
        b"packets 1\n"
        b"tx_errors 0\n"
        b"last_tx_result 0\n"
        b"last_tx_len 5\n"
        b"rx_attempts 1\n"
        b"rx_errors 0\n"
        b"last_rx_result 0\n"
        b"last_rx_len 2\n"
        + f"last_rx_pid 0x{pid_hex}\n".encode()
        + f"last_rx_hex f0{pid_hex}\n".encode()
    )

status = status_for_report("0000040000000000")

def report_for_key(key):
    return f"0000{key:02x}0000000000"

release_report = "0000000000000000"

def run_live(mode):
    master, slave = pty.openpty()
    attrs = termios.tcgetattr(slave)
    attrs[3] &= ~(termios.ECHO | termios.ICANON)
    attrs[0] &= ~(termios.ICRNL | termios.IXON)
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    slave_name = os.ttyname(slave)
    exe = os.path.join(tmp, f"fruitjam-usbhost-{mode}")
    subprocess.run([
        "cc", "-Wall", "-Wextra", "-Wno-deprecated-declarations", "-Os",
        f'-DBRIDGE_DEV="{slave_name}"',
        "-o", exe, src,
    ], check=True)
    flags = fcntl.fcntl(master, fcntl.F_GETFL)
    fcntl.fcntl(master, fcntl.F_SETFL, flags | os.O_NONBLOCK)
    proc = subprocess.Popen(
        [exe, mode, "0"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    seen = bytearray()
    answered = False
    deadline = time.time() + 3
    while proc.poll() is None and time.time() < deadline:
        try:
            chunk = os.read(master, 128)
            if chunk:
                seen.extend(chunk)
        except BlockingIOError:
            pass
        except OSError as exc:
            if exc.errno != errno.EIO:
                raise
        if not answered and b"kbd-poll" in seen:
            os.write(master, status)
            answered = True
        time.sleep(0.01)
    out, err = proc.communicate(timeout=2)
    os.close(slave)
    os.close(master)
    if proc.returncode:
        raise SystemExit(f"{mode} failed rc={proc.returncode} out={out!r} err={err!r} bridge={seen!r}")
    return out.decode("utf-8", "replace"), err.decode("utf-8", "replace"), bytes(seen)

def run_shell():
    master, slave = pty.openpty()
    attrs = termios.tcgetattr(slave)
    attrs[3] &= ~(termios.ECHO | termios.ICANON)
    attrs[0] &= ~(termios.ICRNL | termios.IXON)
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    slave_name = os.ttyname(slave)
    exe = os.path.join(tmp, "fruitjam-usbhost-kbd-shell")
    subprocess.run([
        "cc", "-Wall", "-Wextra", "-Wno-deprecated-declarations", "-Os",
        f'-DBRIDGE_DEV="{slave_name}"',
        "-o", exe, src,
    ], check=True)
    flags = fcntl.fcntl(master, fcntl.F_GETFL)
    fcntl.fcntl(master, fcntl.F_SETFL, flags | os.O_NONBLOCK)
    proc = subprocess.Popen(
        [exe, "kbd-shell", "2"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    keys = [8, 6, 11, 18, 44, 18, 14, 40, 8, 27, 12, 23, 40]
    reports = []
    for key in keys:
        reports.append(report_for_key(key))
        reports.append(release_report)
    seen = bytearray()
    answered = 0
    deadline = time.time() + 5
    while proc.poll() is None and time.time() < deadline:
        try:
            chunk = os.read(master, 256)
            if chunk:
                seen.extend(chunk)
        except BlockingIOError:
            pass
        except OSError as exc:
            if exc.errno != errno.EIO:
                raise
        polls = seen.count(b"kbd-poll")
        while answered < polls and answered < len(reports):
            os.write(master, status_for_report(reports[answered]))
            answered += 1
        time.sleep(0.01)
    out, err = proc.communicate(timeout=2)
    os.close(slave)
    os.close(master)
    if proc.returncode:
        raise SystemExit(f"kbd-shell failed rc={proc.returncode} out={out!r} err={err!r} bridge={seen!r}")
    return out.decode("utf-8", "replace"), err.decode("utf-8", "replace"), bytes(seen)

def run_find():
    master, slave = pty.openpty()
    attrs = termios.tcgetattr(slave)
    attrs[3] &= ~(termios.ECHO | termios.ICANON)
    attrs[0] &= ~(termios.ICRNL | termios.IXON)
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    slave_name = os.ttyname(slave)
    exe = os.path.join(tmp, "fruitjam-usbhost-kbd-find")
    subprocess.run([
        "cc", "-Wall", "-Wextra", "-Wno-deprecated-declarations", "-Os",
        f'-DBRIDGE_DEV="{slave_name}"',
        "-o", exe, src,
    ], check=True)
    flags = fcntl.fcntl(master, fcntl.F_GETFL)
    fcntl.fcntl(master, fcntl.F_SETFL, flags | os.O_NONBLOCK)
    proc = subprocess.Popen(
        [exe, "kbd-find"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    seen = bytearray()
    answered_ep1 = False
    answered_ep2 = False
    deadline = time.time() + 5
    while proc.poll() is None and time.time() < deadline:
        try:
            chunk = os.read(master, 256)
            if chunk:
                seen.extend(chunk)
        except BlockingIOError:
            pass
        except OSError as exc:
            if exc.errno != errno.EIO:
                raise
        if b"kbd-poll 1 1" in seen and not answered_ep1:
            os.write(master, status_for_pid("1e"))
            answered_ep1 = True
        if b"kbd-poll 1 2" in seen and not answered_ep2:
            os.write(master, status_for_report("0000050000000000"))
            answered_ep2 = True
        time.sleep(0.01)
    out, err = proc.communicate(timeout=2)
    os.close(slave)
    os.close(master)
    if proc.returncode:
        raise SystemExit(f"kbd-find failed rc={proc.returncode} out={out!r} err={err!r} bridge={seen!r}")
    return out.decode("utf-8", "replace"), err.decode("utf-8", "replace"), bytes(seen)

events, events_err, events_bridge = run_live("kbd-events")
text, text_err, text_bridge = run_live("kbd-text")
if "press key=a char=a code=0x04 modifiers=0x00" not in events:
    raise SystemExit(f"fruitjam-usbhost kbd-events did not emit key press: {events!r} {events_err!r}")
if text != "a\n":
    raise SystemExit(f"fruitjam-usbhost kbd-text did not emit text: {text!r} {text_err!r}")
if b"kbd-init 1 1 0" not in events_bridge or b"kbd-poll 1 1" not in text_bridge:
    raise SystemExit("fruitjam-usbhost did not write parameterized keyboard bridge commands")
shell, shell_err, shell_bridge = run_shell()
if "USB keyboard shell; type exit to leave" not in shell or "echo ok" not in shell or "\nok\n" not in shell:
    raise SystemExit(f"fruitjam-usbhost kbd-shell did not run typed command: {shell!r} {shell_err!r}")
if b"kbd-init 1 1 0" not in shell_bridge or b"kbd-poll 1 1" not in shell_bridge:
    raise SystemExit("fruitjam-usbhost kbd-shell did not use parameterized keyboard commands")
found, found_err, found_bridge = run_find()
if "usbhost keyboard target addr=1 config=1 iface=0 ep=2 source=report" not in found:
    raise SystemExit(f"fruitjam-usbhost kbd-find did not select report endpoint: {found!r} {found_err!r}")
if b"kbd-poll 1 1" not in found_bridge or b"kbd-poll 1 2" not in found_bridge:
    raise SystemExit("fruitjam-usbhost kbd-find did not scan endpoints")
print("ok fruitjam-usbhost live keyboard text/events")
print("ok fruitjam-usbhost keyboard shell")
print("ok fruitjam-usbhost keyboard auto-find")
PY
python3 - "$tmp/fruitjam-usbhost.txt" "$tmp/fruitjam-usbhost.json" "$tmp/fruitjam-usbhost-wait.txt" "$tmp/fruitjam-usbhost-monitor.txt" "$tmp/fruitjam-usbhost-reset.txt" "$gpio_root" "$tmp/fruitjam-usbhost-decode-direct.txt" "$tmp/fruitjam-usbhost-decode-bridge.txt" "$tmp/fruitjam-usbhost-decode-hid.txt" "$tmp/fruitjam-usbhost-hid-packet.txt" "$tmp/fruitjam-usbhost-hid-raw.txt" "$tmp/fruitjam-usbhost-hid-descriptor.txt" <<'PY'
import json
import sys
from pathlib import Path

text = Path(sys.argv[1]).read_text()
data = json.loads(Path(sys.argv[2]).read_text())
wait = Path(sys.argv[3]).read_text()
monitor = Path(sys.argv[4]).read_text()
reset = Path(sys.argv[5]).read_text()
gpio_root = Path(sys.argv[6])
decode_direct = Path(sys.argv[7]).read_text()
decode_bridge = Path(sys.argv[8]).read_text()
decode_hid = Path(sys.argv[9]).read_text()
hid_packet = Path(sys.argv[10]).read_text()
hid_raw = Path(sys.argv[11]).read_text()
hid_descriptor = Path(sys.argv[12]).read_text()
if "usbhost device full-speed-device" not in text:
    raise SystemExit("fruitjam-usbhost status did not classify full-speed device")
if "pio-packet-io" not in text:
    raise SystemExit("fruitjam-usbhost status missing next HID bridge hint")
if data.get("device") != "full-speed-device" or data.get("present") is not True:
    raise SystemExit("fruitjam-usbhost json device/present fields regressed")
if data.get("hid") is not False or data.get("driver") != "sysfs-line-state":
    raise SystemExit("fruitjam-usbhost json must not claim HID stack support yet")
if data.get("first_milestone") != "boot-protocol-keyboard":
    raise SystemExit("fruitjam-usbhost json missing boot keyboard milestone")
if "full-speed-device" not in wait or "full-speed-device" not in monitor:
    raise SystemExit("fruitjam-usbhost wait/monitor did not report device state")
if "usbhost reset 10ms" not in reset or "usbhost device no-device-or-reset" not in reset:
    raise SystemExit("fruitjam-usbhost reset did not report reset on fake GPIO")
if not (gpio_root / "gpio1" / "direction").read_text().startswith("in"):
    raise SystemExit("fruitjam-usbhost reset did not release D+ to input")
if not (gpio_root / "gpio2" / "direction").read_text().startswith("in"):
    raise SystemExit("fruitjam-usbhost reset did not release D- to input")
if "pid=DATA0" not in decode_direct or "payload-bytes=8" not in decode_direct:
    raise SystemExit("fruitjam-usbhost direct packet decode regressed")
if "descriptor device-prefix" not in decode_direct or "maxpkt0=64" not in decode_direct:
    raise SystemExit("fruitjam-usbhost device descriptor decode regressed")
if "prefix-bytes=1" not in decode_bridge or "pid=DATA1" not in decode_bridge:
    raise SystemExit("fruitjam-usbhost bridge-format packet decode regressed")
if "hid boot-keyboard modifiers=0x00 keys=a(0x04)" not in decode_hid:
    raise SystemExit("fruitjam-usbhost HID packet hint regressed")
if 'hid-text "a"' not in decode_hid:
    raise SystemExit("fruitjam-usbhost HID text hint regressed")
if "usbhost hid boot-keyboard modifiers=0x00 keys=a(0x04)" not in hid_packet:
    raise SystemExit("fruitjam-usbhost hid packet decode regressed")
if 'usbhost hid-text "a"' not in hid_packet:
    raise SystemExit("fruitjam-usbhost hid packet text regressed")
if "usbhost hid boot-keyboard modifiers=0x02 keys=b(0x05)" not in hid_raw:
    raise SystemExit("fruitjam-usbhost hid raw report decode regressed")
if 'usbhost hid-text "B"' not in hid_raw:
    raise SystemExit("fruitjam-usbhost hid raw report text regressed")
if "not-boot-keyboard-report" not in hid_descriptor:
    raise SystemExit("fruitjam-usbhost hid descriptor rejection regressed")
PY
echo "ok fruitjam-usbhost status"
echo "ok fruitjam-usbhost json wait monitor reset"
echo "ok fruitjam-usbhost rx decode"
echo "ok fruitjam-usbhost HID packet decode"
echo "ok fruitjam-usbhost hid command"

echo "== hid boot-keyboard decoder host behavior =="
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-o "$tmp/fruitjam-hidkeys" "$hidkeys_src"
"$tmp/fruitjam-hidkeys" \
	0000040000000000 \
	0000000000000000 \
	0200050000000000 \
	0000000000000000 \
	0000280000000000 > "$tmp/fruitjam-hidkeys.txt"
"$tmp/fruitjam-hidkeys" \
	0000040000000000 \
	0000040000000000 \
	0000000000000000 > "$tmp/fruitjam-hidkeys-held.txt"
printf '00:00:04:00:00:00:00:00\n00:00:00:00:00:00:00:00\n' | \
	"$tmp/fruitjam-hidkeys" --events > "$tmp/fruitjam-hidkeys-events.txt"
"$tmp/fruitjam-hidkeys" \
	f04b0000040000000000abcd \
	f04b0000000000000000abcd > "$tmp/fruitjam-hidkeys-packet.txt"
"$tmp/fruitjam-hidkeys" --events \
	f04b0000040000000000abcd \
	f04b0000000000000000abcd > "$tmp/fruitjam-hidkeys-packet-events.txt"
if "$tmp/fruitjam-hidkeys" f04b12010002000000403412 > "$tmp/fruitjam-hidkeys-descriptor.txt" 2> "$tmp/fruitjam-hidkeys-descriptor.err"; then
	echo "fruitjam-hidkeys accepted a descriptor packet as keyboard input" >&2
	exit 1
fi
python3 - "$tmp/fruitjam-hidkeys.txt" "$tmp/fruitjam-hidkeys-held.txt" "$tmp/fruitjam-hidkeys-events.txt" "$tmp/fruitjam-hidkeys-packet.txt" "$tmp/fruitjam-hidkeys-packet-events.txt" "$tmp/fruitjam-hidkeys-descriptor.err" <<'PY'
import sys
from pathlib import Path

text = Path(sys.argv[1]).read_text()
held = Path(sys.argv[2]).read_text()
events = Path(sys.argv[3]).read_text()
packet = Path(sys.argv[4]).read_text()
packet_events = Path(sys.argv[5]).read_text()
descriptor_err = Path(sys.argv[6]).read_text()
if text != "aB\n":
    raise SystemExit(f"fruitjam-hidkeys text decode regressed: {text!r}")
if held != "a":
    raise SystemExit(f"fruitjam-hidkeys repeated held key: {held!r}")
if "press key=a char=a code=0x04 modifiers=0x00" not in events:
    raise SystemExit("fruitjam-hidkeys events missing press")
if "release key=a code=0x04" not in events:
    raise SystemExit("fruitjam-hidkeys events missing release")
if packet != "a":
    raise SystemExit(f"fruitjam-hidkeys packet decode regressed: {packet!r}")
if "press key=a char=a code=0x04 modifiers=0x00" not in packet_events:
    raise SystemExit("fruitjam-hidkeys packet events missing press")
if "release key=a code=0x04" not in packet_events:
    raise SystemExit("fruitjam-hidkeys packet events missing release")
if "not a boot-keyboard report" not in descriptor_err:
    raise SystemExit("fruitjam-hidkeys descriptor packet rejection regressed")
PY
echo "ok fruitjam-hidkeys text"
echo "ok fruitjam-hidkeys events"
echo "ok fruitjam-hidkeys USB packet decode"

echo "== mqtt pub/sub host behavior =="
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-o "$tmp/mosquitto_pub" "$mosquitto_pub_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-o "$tmp/mosquitto_sub" "$mosquitto_sub_src"
python3 - "$tmp/mosquitto_pub" "$tmp/mosquitto_sub" <<'PY'
import socket
import struct
import subprocess
import sys
import threading

pub_exe, sub_exe = sys.argv[1:3]

def read_exact(conn, n):
    data = bytearray()
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            raise RuntimeError("short read")
        data.extend(chunk)
    return bytes(data)

def read_remaining(conn):
    multiplier = 1
    value = 0
    while True:
        b = read_exact(conn, 1)[0]
        value += (b & 127) * multiplier
        if not b & 128:
            return value
        multiplier *= 128

def read_packet(conn):
    header = read_exact(conn, 1)[0]
    length = read_remaining(conn)
    return header, read_exact(conn, length)

def get_string(packet, pos):
    size = struct.unpack("!H", packet[pos:pos + 2])[0]
    pos += 2
    return packet[pos:pos + size].decode(), pos + size

def check_connect(packet):
    proto, pos = get_string(packet, 0)
    assert proto == "MQTT"
    assert packet[pos] == 4
    flags = packet[pos + 1]
    pos += 4
    client, pos = get_string(packet, pos)
    user = password = None
    if flags & 0x80:
        user, pos = get_string(packet, pos)
    if flags & 0x40:
        password, pos = get_string(packet, pos)
    assert client
    assert user == "user"
    assert password == "pass"

def serve_once(handler):
    ready = {}
    errors = []
    def run():
        try:
            with socket.socket() as srv:
                srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                srv.bind(("127.0.0.1", 0))
                srv.listen(1)
                ready["port"] = srv.getsockname()[1]
                conn, _ = srv.accept()
                with conn:
                    handler(conn)
        except BaseException as exc:
            errors.append(exc)
    thread = threading.Thread(target=run)
    thread.start()
    while "port" not in ready and not errors:
        pass
    if errors:
        raise errors[0]
    return ready["port"], thread, errors

published = {}
def pub_handler(conn):
    header, packet = read_packet(conn)
    assert header == 0x10
    check_connect(packet)
    conn.sendall(b"\x20\x02\x00\x00")
    header, packet = read_packet(conn)
    assert header == 0x30
    topic, pos = get_string(packet, 0)
    published["topic"] = topic
    published["payload"] = packet[pos:].decode()
    read_packet(conn)

port, thread, errors = serve_once(pub_handler)
subprocess.run([
    pub_exe, "-h", "127.0.0.1", "-p", str(port), "-u", "user", "-P", "pass",
    "-i", "fruitjam-test-pub", "-t", "charlie/test", "-m", "hello"
], check=True)
thread.join(timeout=2)
if errors:
    raise errors[0]
assert published == {"topic": "charlie/test", "payload": "hello"}

def sub_handler(conn):
    header, packet = read_packet(conn)
    assert header == 0x10
    check_connect(packet)
    conn.sendall(b"\x20\x02\x00\x00")
    header, packet = read_packet(conn)
    assert header == 0x82
    assert packet[:2] == b"\x00\x01"
    topic, pos = get_string(packet, 2)
    assert topic == "charlie/#"
    assert packet[pos] == 0
    conn.sendall(b"\x90\x03\x00\x01\x00")
    publish = b"\x00\x0ccharlie/testhello"
    conn.sendall(b"\x30" + bytes([len(publish)]) + publish)

port, thread, errors = serve_once(sub_handler)
result = subprocess.run([
    sub_exe, "-h", "127.0.0.1", "-p", str(port), "-u", "user", "-P", "pass",
    "-i", "fruitjam-test-sub", "-t", "charlie/#", "-C", "1", "-W", "3", "-v"
], check=True, text=True, stdout=subprocess.PIPE)
thread.join(timeout=2)
if errors:
    raise errors[0]
if result.stdout != "charlie/test hello\n":
    raise SystemExit(f"mosquitto_sub output regressed: {result.stdout!r}")
print("ok mqtt pub auth")
print("ok mqtt sub auth")
PY

echo "== usbhost kernel bridge source guards =="
python3 - "$kernel_usbhost_patch" "$kernel_usbhost_pio_patch" "$kernel_usbhost_tx_patch" "$kernel_usbhost_rx_patch" "$kernel_usbhost_dma_patch" "$kernel_usbhost_reset_patch" "$kernel_usbhost_reloc_patch" "$kernel_usbhost_rx_osr_patch" "$kernel_usbhost_selfrx_patch" "$kernel_usbhost_eop_patch" "$kernel_usbhost_eop_reset_patch" "$kernel_usbhost_tx_latch_patch" "$kernel_usbhost_tx_idle_patch" "$kernel_usbhost_tx_eop_patch" "$kernel_usbhost_debug_patch" "$kernel_usbhost_debug_finish_patch" "$kernel_usbhost_gated_patch" "$kernel_usbhost_gated_write_patch" "$kernel_usbhost_dma_eop_patch" "$kernel_usbhost_dma_idle_patch" "$kernel_usbhost_rx_drain_patch" "$kernel_usbhost_setup_selfrx_patch" "$kernel_usbhost_rx_tail_patch" "$kernel_usbhost_cpu_tx_patch" "$kernel_usbhost_noeop_patch" "$kernel_usbhost_sweep_patch" "$kernel_usbhost_empty_eop_patch" "$kernel_usbhost_clock_diag_patch" "$kernel_usbhost_active_sof_patch" "$kernel_usbhost_combo_patch" "$kernel_usbhost_fast_patch" "$kernel_usbhost_tight_patch" "$kernel_usbhost_burst_patch" "$kernel_usbhost_stream_patch" "$kernel_usbhost_stream_wait_patch" "$kernel_usbhost_live_drain_patch" "$kernel_usbhost_low_speed_patch" "$kernel_usbhost_tx_eop_gated_patch" "$kernel_usbhost_combo_skipack_patch" "$kernel_usbhost_keyboard_target_patch" "$kernel_config_src" "$dts_src" "$usbhost_src" "$web_cgi_src" "$airlift_src" "$bootloader_clocks_src" "$web_page_src" <<'PY'
import sys
from pathlib import Path

patch = Path(sys.argv[1]).read_text()
pio_patch = Path(sys.argv[2]).read_text()
tx_patch = Path(sys.argv[3]).read_text()
rx_patch = Path(sys.argv[4]).read_text()
dma_patch = Path(sys.argv[5]).read_text()
reset_patch = Path(sys.argv[6]).read_text()
reloc_patch = Path(sys.argv[7]).read_text()
rx_osr_patch = Path(sys.argv[8]).read_text()
selfrx_patch = Path(sys.argv[9]).read_text()
eop_patch = Path(sys.argv[10]).read_text()
eop_reset_patch = Path(sys.argv[11]).read_text()
tx_latch_patch = Path(sys.argv[12]).read_text()
tx_idle_patch = Path(sys.argv[13]).read_text()
tx_eop_patch = Path(sys.argv[14]).read_text()
debug_patch = Path(sys.argv[15]).read_text() + "\n" + Path(sys.argv[16]).read_text()
gated_patch = Path(sys.argv[17]).read_text() + "\n" + Path(sys.argv[18]).read_text()
dma_eop_patch = Path(sys.argv[19]).read_text()
dma_idle_patch = Path(sys.argv[20]).read_text()
rx_drain_patch = Path(sys.argv[21]).read_text()
setup_selfrx_patch = Path(sys.argv[22]).read_text()
rx_tail_patch = Path(sys.argv[23]).read_text()
cpu_tx_patch = Path(sys.argv[24]).read_text()
noeop_patch = Path(sys.argv[25]).read_text()
sweep_patch = Path(sys.argv[26]).read_text()
empty_eop_patch = Path(sys.argv[27]).read_text()
clock_diag_patch = Path(sys.argv[28]).read_text()
active_sof_patch = Path(sys.argv[29]).read_text()
combo_patch = Path(sys.argv[30]).read_text()
fast_patch = Path(sys.argv[31]).read_text()
tight_patch = Path(sys.argv[32]).read_text()
burst_patch = Path(sys.argv[33]).read_text()
stream_patch = Path(sys.argv[34]).read_text()
stream_wait_patch = Path(sys.argv[35]).read_text()
live_drain_patch = Path(sys.argv[36]).read_text()
low_speed_patch = Path(sys.argv[37]).read_text()
tx_eop_gated_patch = Path(sys.argv[38]).read_text()
combo_skipack_patch = Path(sys.argv[39]).read_text()
keyboard_target_patch = Path(sys.argv[40]).read_text()
config = Path(sys.argv[41]).read_text()
dts = Path(sys.argv[42]).read_text()
helper = Path(sys.argv[43]).read_text()
cgi = Path(sys.argv[44]).read_text()
airlift = Path(sys.argv[45]).read_text()
clocks = Path(sys.argv[46]).read_text()
web = Path(sys.argv[47]).read_text()
if "CONFIG_FRUITJAM_USBHOST_BRIDGE" not in patch or "fruitjam_usbhost.c" not in patch:
    raise SystemExit("kernel patch missing Fruit Jam USB host bridge driver")
if "/dev/fruitjam-usbhost" not in patch or "pio-packet-io-pending" not in patch:
    raise SystemExit("kernel USB host bridge patch missing device/status contract")
for needle in (
    "FJ_USBHOST_PIO_INDEX_DEFAULT\t2u",
    "FJ_USBHOST_PIO_PROGRAM_WORDS\t32u",
    "fj_usbhost_pio_program",
    "pio-packet-io-program-loaded",
):
    if needle not in pio_patch:
        raise SystemExit(f"kernel USB host PIO patch missing {needle}")
for needle in (
    "fj_usbhost_pio_configure_sms",
    "FJ_PIO_SM_CLKDIV",
    "FJ_PIO_TX_START_INSTR",
    "fj_usbhost_pio_tx_test",
    "pio_configured",
    "tx-test",
):
    if needle not in tx_patch:
        raise SystemExit(f"kernel USB host TX patch missing {needle}")
for needle in (
    "FJ_IO_GPIO_CTRL_INOVER_INVERT",
    "FJ_PIO_IRQ_RX_EOP",
    "fj_usbhost_pio_receive_raw",
    "fj_usbhost_pio_get_device8",
    "last_rx_hex",
    "in-token",
    "get-device-8",
):
    if needle not in rx_patch:
        raise SystemExit(f"kernel USB host RX patch missing {needle}")
for needle in (
    "FJ_USBHOST_TX_DMA_CHANNEL_DEFAULT 2u",
    "FJ_DMA_CTRL_TREQ",
    "fj_usbhost_pio_tx_encoded_dma",
    "tx_dma_packets",
    "last_dma_ctrl",
):
    if needle not in dma_patch:
        raise SystemExit(f"kernel USB host DMA patch missing {needle}")
for needle in (
    "FJ_IO_GPIO_CTRL_OUTOVER_LOW",
    "FJ_IO_GPIO_CTRL_OEOVER_HIGH",
    "fj_gpio_bus_reset_override",
    "fj_usbhost_bus_reset_override",
    "bool use_pio = uh->pio_ready",
):
    if needle not in reset_patch:
        raise SystemExit(f"kernel USB host reset patch missing {needle}")
for needle in (
    "FJ_PIO_INSTR_JMP",
    "FJ_PIO_INSTR_ADDR_MASK",
    "fj_usbhost_pio_program_offset",
    "fj_usbhost_pio_relocate_instr",
):
    if needle not in reloc_patch:
        raise SystemExit(f"kernel USB host PIO relocation patch missing {needle}")
for needle in (
    "FJ_PIO_RX_OSR_ONES_INSTR\t0xa0ebu",
    "FJ_PIO_RX_SET_X_ZERO_INSTR\t0xe020u",
):
    if needle not in rx_osr_patch:
        raise SystemExit(f"kernel USB host RX OSR patch missing {needle}")
for needle in (
    "fj_usbhost_pio_self_rx",
    "self-rx",
    "Do not clear RX IRQs here",
):
    if needle not in selfrx_patch:
        raise SystemExit(f"kernel USB host self-RX patch missing {needle}")
for needle in (
    "FJ_PIO_EOP_INIT_INSTR\t\t(FJ_PIO_EOP_OFFSET)",
    "eop_sm + FJ_PIO_SM_INSTR",
    "fj_usbhost_pio_clear_rx_irq(uh);",
):
    if needle not in eop_patch:
        raise SystemExit(f"kernel USB host EOP init patch missing {needle}")
for needle in (
    "fj_usbhost_pio_sm_disable(uh, uh->sm_eop)",
    "fj_usbhost_pio_sm_restart(uh, uh->sm_eop)",
    "FJ_PIO_EOP_INIT_INSTR",
    "fj_usbhost_pio_sm_enable(uh, uh->sm_eop)",
):
    if needle not in eop_reset_patch:
        raise SystemExit(f"kernel USB host EOP reset patch missing {needle}")
for needle in (
    "fj_usbhost_pio_set_pin_latch",
    "FJ_PIO_SET_PINS_INSTR",
    "uh->dp_gpio, true",
    "uh->dm_gpio, false",
):
    if needle not in tx_latch_patch:
        raise SystemExit(f"kernel USB host TX idle latch patch missing {needle}")
if "FJ_PIO_TX_INIT_INSTR\t\t(FJ_PIO_TX_J_SIDE | 2u)" not in tx_idle_patch:
    raise SystemExit("kernel USB host TX idle patch must reset TX SM to offset + 2")
if "TXSTALL can latch during DMA startup or idle; EOP is completion" not in tx_eop_patch:
    raise SystemExit("kernel USB host TX EOP patch must document why TXSTALL is not completion")
if "\n+\t\tif (fdebug & FJ_PIO_FDEBUG_TXSTALL" in tx_eop_patch:
    raise SystemExit("kernel USB host TX EOP patch must remove TXSTALL completion")
for needle in (
    "pio_ctrl 0x%08x",
    "tx_addr %u",
    "rx_addr %u",
    "eop_addr %u",
    "dp_ctrl 0x%08x",
    "dm_ctrl 0x%08x",
    "kzalloc(2048, GFP_KERNEL)",
    "kfree(tmp)",
):
    if needle not in debug_patch:
        raise SystemExit(f"kernel USB host debug patch missing {needle}")
for needle in (
    "FJ_PIO_WAIT_TX_EOP_INSTR",
    "fj_usbhost_pio_prepare_receive_after_tx",
    "fj_usbhost_pio_in_token_gated",
    "fj_usbhost_pio_get_device8_gated",
    "in-token-gated",
    "get-device-8-gated",
):
    if needle not in gated_patch:
        raise SystemExit(f"kernel USB host gated RX patch missing {needle}")
for needle in (
    "Leave TX-EOP latched for fj_usbhost_pio_wait_tx_complete",
    "packets can finish before the CPU reaches this point",
):
    if needle not in dma_eop_patch:
        raise SystemExit(f"kernel USB host DMA EOP patch missing {needle}")
for needle in (
    "fj_usbhost_pio_dma_tx_idle",
    "FJ_DMA_CTRL_BUSY",
    "FJ_PIO_TX_WRAP_TARGET",
    "FJ_PIO_FDEBUG_TXSTALL",
):
    if needle not in dma_idle_patch:
        raise SystemExit(f"kernel USB host DMA idle patch missing {needle}")
for needle in (
    "FJ_USBHOST_DRAIN_RX_FIFO",
    "drain_waited < 50",
):
    if needle not in rx_drain_patch:
        raise SystemExit(f"kernel USB host RX drain patch missing {needle}")
for needle in (
    "fj_usbhost_pio_setup_data_self_rx",
    "setup-data-self-rx",
    "fj_usbhost_pio_send_data0",
):
    if needle not in setup_selfrx_patch:
        raise SystemExit(f"kernel USB host setup self-RX patch missing {needle}")
for needle in (
    "quiet < 8",
    "drain_waited < 50",
    "u32 before = len",
):
    if needle not in rx_tail_patch:
        raise SystemExit(f"kernel USB host RX tail patch missing {needle}")
for needle in (
    "fj_usbhost_pio_tx_packet_cpu",
    "fj_usbhost_pio_send_data0_cpu",
    "setup-data-self-rx-cpu",
    "get-device-8-gated-cpu",
):
    if needle not in cpu_tx_patch:
        raise SystemExit(f"kernel USB host CPU TX patch missing {needle}")
for needle in (
    "fj_usbhost_pio_receive_raw_noeop_stop",
    "setup-data-self-rx-noeop",
    "FJ_PIO_IRQ_RX_EOP",
):
    if needle not in noeop_patch:
        raise SystemExit(f"kernel USB host no-EOP patch missing {needle}")
for needle in (
    "fj_usbhost_pio_tx_encoded_capture_dma",
    "setup-data-self-rx-drain",
    "expected=12",
    "live-drain diagnostic",
):
    if needle not in live_drain_patch:
        raise SystemExit(f"kernel USB host live-drain patch missing {needle}")
for needle in (
    "fj_usbhost_tx_program_dmdp",
    "fj_usbhost_low_speed_line",
    "FJ_PIO_TX_START_INSTR\t\tfj_usbhost_pio_tx_start_instr(uh)",
    "fj_usbhost_pio_tx_init_instr",
    "6000000u : 48000000u",
    "12000000u : 96000000u",
):
	if needle not in low_speed_patch:
		raise SystemExit(f"kernel USB host low-speed patch missing {needle}")
for needle in (
    "fj_usbhost_pio_tx_comp_done",
    "TX-complete helper also consumed that IRQ",
    "writel(FJ_PIO_IRQ_TX_EOP, uh->pio + FJ_PIO_IRQ)",
):
    if needle not in tx_eop_gated_patch:
        raise SystemExit(f"kernel USB host gated TX-EOP patch missing {needle}")
for needle in (
	"fj_usbhost_pio_data_len_sweep",
    "data-len-sweep",
    "probe_summary %s",
    "FJ_USBHOST_PROBE_SUMMARY_MAX",
):
    if needle not in sweep_patch:
        raise SystemExit(f"kernel USB host DATA length sweep patch missing {needle}")
for needle in (
    "Gated receive can start in the host EOP window",
    "writel(FJ_PIO_IRQ_RX_ALL, uh->pio + FJ_PIO_IRQ)",
    "continue;",
):
    if needle not in empty_eop_patch:
        raise SystemExit(f"kernel USB host empty-EOP patch missing {needle}")
for needle in (
    "FJ_USBHOST_CLK_SYS_HZ_DEFAULT\t144000000u",
    "clk_sys_hz %u",
    "tx_clkdiv 0x%08x",
    "eop_clkdiv 0x%08x",
):
    if needle not in clock_diag_patch:
        raise SystemExit(f"kernel USB host clock diagnostics patch missing {needle}")
for needle in (
    "FJ_USBHOST_SOF_BURST_FRAMES",
    "fj_usbhost_pio_send_sof",
    "fj_usbhost_pio_setup_token_self_rx",
    "fj_usbhost_pio_reset_active",
    "reset-get-device-8",
    "reset-get-device-8-gated",
):
    if needle not in active_sof_patch:
        raise SystemExit(f"kernel USB host active SOF patch missing {needle}")
for needle in (
    "fj_usbhost_pio_capture_drain",
    "fj_usbhost_pio_tx_encoded_capture_cpu",
    "fj_usbhost_pio_tx_two_packets_capture_cpu",
    "fj_usbhost_pio_send_setup_data0_combo",
    "fj_usbhost_pio_receive_capture",
    "fj_usbhost_pio_get_device8_combo",
    "get-device-8-combo",
    "reset-get-device-8-combo",
):
    if needle not in combo_patch:
        raise SystemExit(f"kernel USB host combined SETUP patch missing {needle}")
for needle in (
    "fj_usbhost_pio_get_device8_combo_skipack",
    "get-device-8-combo-skipack",
    "reset-get-device-8-combo-skipack",
    "combo-skipack setup-rx-len=%u setup-rx-pid=0x%02x",
):
    if needle not in combo_skipack_patch:
        raise SystemExit(f"kernel USB host combined skip-ACK patch missing {needle}")
for needle in (
    "kbd-init ADDR CONFIG IFACE",
    "kbd-poll ADDR EP",
    "FJ_USBHOST_BOOT_KEYBOARD_CONFIG",
    "fj_usbhost_parse_kbd_init",
    "fj_usbhost_parse_kbd_poll",
    "fj_usbhost_pio_keyboard_poll(uh, kbd_addr",
    "fj_usbhost_pio_keyboard_init_poll(",
):
    if needle not in keyboard_target_patch:
        raise SystemExit(f"kernel USB host keyboard target patch missing {needle}")
for needle in (
    "fj_usbhost_pio_get_device8_fast",
    "get-device-8-fast",
    "reset-get-device-8-fast",
    "Do not record TX debug state between SETUP token and DATA0",
):
    if needle not in fast_patch:
        raise SystemExit(f"kernel USB host fast SETUP patch missing {needle}")
for needle in (
    "fj_usbhost_pio_get_device8_tight",
    "get-device-8-tight",
    "reset-get-device-8-tight",
    "DATA0 packet before waiting",
):
    if needle not in tight_patch:
        raise SystemExit(f"kernel USB host tight SETUP patch missing {needle}")
for needle in (
    "fj_usbhost_pio_get_device8_burst",
    "get-device-8-burst",
    "reset-get-device-8-burst",
    "skip the ACK wait",
):
    if needle not in burst_patch:
        raise SystemExit(f"kernel USB host burst SETUP patch missing {needle}")
for needle in (
    "fj_usbhost_encode_tx_pair_no_release",
    "fj_usbhost_pio_send_setup_data0_stream",
    "fj_usbhost_pio_get_device8_stream",
    "get-device-8-stream",
    "reset-get-device-8-stream",
):
    if needle not in stream_patch:
        raise SystemExit(f"kernel USB host streamed SETUP patch missing {needle}")
for needle in (
    "fj_usbhost_pio_wait_tx_stream_idle",
    "A streamed setup has one EOP per packet",
    "fj_usbhost_pio_wait_tx_stream_idle(uh)",
):
    if needle not in stream_wait_patch:
        raise SystemExit(f"kernel USB host streamed TX wait patch missing {needle}")
if "CONFIG_FRUITJAM_USBHOST_BRIDGE=y" not in config:
    raise SystemExit("Fruit Jam kernel config does not enable USB host bridge")
if "adafruit,fruit-jam-rp2350-usbhost" not in dts or "usbhost-bridge@d0000000" not in dts:
    raise SystemExit("Fruit Jam DTS missing USB host bridge node")
if "clock-frequency = <144000000>" not in dts or "raspberrypi,clk-sys-hz = <144000000>" not in dts:
    raise SystemExit("Fruit Jam DTS must keep clk_sys at 144 MHz for exact PIO USB 48 MHz timing")
if "#define PLL_SYS_HZ\t(144UL * MHZ)" not in clocks or "PLL_FBDIV_INT_REG] = 120" not in clocks:
    raise SystemExit("Fruit Jam bootloader must keep clk_sys at 144 MHz for exact PIO USB 48 MHz timing")
for needle in ("0x50400000", "raspberrypi,pio = <2>", "raspberrypi,sm-tx = <0>",
               "raspberrypi,sm-rx = <1>", "raspberrypi,sm-eop = <2>",
               "0x50000000", "\"pio\", \"resets\", \"dma\"",
               "raspberrypi,tx-dma-channel = <2>"):
    if needle not in dts:
        raise SystemExit(f"Fruit Jam DTS missing USB host PIO2 setting {needle}")
for source, name in ((helper, "fruitjam-usbhost"), (cgi, "CGI"), (airlift, "AirLift")):
    if "/dev/fruitjam-usbhost" not in source:
        raise SystemExit(f"{name} does not use the kernel USB host bridge when present")
    if "pio-packet-io" not in source:
        raise SystemExit(f"{name} missing next PIO packet I/O milestone")
    if "pio_ready" not in source:
        raise SystemExit(f"{name} does not surface PIO readiness")
if "pio-init" not in helper or "tx-test" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose PIO init/TX test")
if "self-rx" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose self-RX diagnostic")
if "setup-data-self-rx" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose SETUP DATA self-RX diagnostic")
if "setup-data-self-rx-noeop" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose no-EOP self-RX diagnostic")
if "setup-data-self-rx-cpu" not in helper or "get-device-8-gated-cpu" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose CPU TX diagnostics")
if "setup-data-self-rx-drain" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose live-drain self-RX diagnostic")
if "data-len-sweep" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose DATA length sweep diagnostic")
if "sof-burst" not in helper or "setup-token-self-rx" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose SOF/token diagnostics")
if "reset-get-device-8" not in helper or "reset-get-device-8-gated" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose reset/SOF GET_DESCRIPTOR diagnostics")
if "get-device-8-combo" not in helper or "reset-get-device-8-combo" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose combined SETUP diagnostics")
if "get-device-8-combo-skipack" not in helper or "reset-get-device-8-combo-skipack" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose combined SETUP skip-ACK diagnostics")
if "get-device-8-fast" not in helper or "reset-get-device-8-fast" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose fast SETUP diagnostics")
if "get-device-8-tight" not in helper or "reset-get-device-8-tight" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose tight SETUP diagnostics")
if "get-device-8-burst" not in helper or "reset-get-device-8-burst" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose burst SETUP diagnostics")
if "get-device-8-stream" not in helper or "reset-get-device-8-stream" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose streamed SETUP diagnostics")
if "in-token-gated" not in helper or "get-device-8-gated" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose gated RX diagnostics")
if "pio_configured" not in helper or "last_tx_result" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not surface PIO TX counters")
if "decode [RX-HEX]" not in helper or "descriptor device-prefix" not in helper:
    raise SystemExit("fruitjam-usbhost helper missing RX descriptor decoder")
if "hid [RX-HEX|REPORT-HEX]" not in helper or "decode_hid_hex" not in helper or "boot-keyboard modifiers" not in helper:
    raise SystemExit("fruitjam-usbhost helper missing HID packet decoder")
if "kbd-text [seconds [addr config iface ep]]" not in helper or "kbd-events [seconds [addr config iface ep]]" not in helper or "keyboard_live" not in helper:
    raise SystemExit("fruitjam-usbhost helper missing live keyboard text/events")
for needle in (
    "kbd-init [addr config iface]",
    "kbd-poll [addr ep]",
    "kbd-find",
    "kbd-auto-text [seconds]",
    "kbd-auto-shell [seconds]",
    "kbd-shell [seconds [addr config iface ep]]",
    "keyboard_init_command",
    "keyboard_poll_command",
    "keyboard_find_target",
    "KBD_SCAN_EP_MAX",
    "keyboard_shell",
):
    if needle not in helper:
        raise SystemExit(f"fruitjam-usbhost helper missing keyboard target/shell support: {needle}")
for source, name in ((helper, "fruitjam-usbhost"), (cgi, "CGI"), (airlift, "AirLift")):
    for needle in (
        "in-token",
        "get-device-8",
        "get-device-8-combo-skipack",
        "reset-get-device-8-combo-skipack",
        "last_rx_result",
        "last_rx_hex",
    ):
        if needle not in source:
            raise SystemExit(f"{name} does not surface USB host RX probe field {needle}")
for needle in (
    "decodeUsbRx",
    "hidKeyName",
    "rx hid: boot keyboard report",
    "rx hid text",
    "usbPidName",
    "rx descriptor",
    "boot keyboard interface yes",
    "formatUsbhost(data)",
):
    if needle not in web:
        raise SystemExit(f"web page missing USB-host RX decode helper: {needle}")
print("ok usbhost kernel bridge source")
PY

echo "== console source guards =="
python3 -m py_compile "$cdc_smoke_src"
grep -q -- '--usb-keyboard' "$cdc_smoke_src"
grep -q -- 'usbhost_keyboard_tests' "$cdc_smoke_src"
echo "ok cdc smoke usb keyboard guard"
grep -q 'ttyAMA0::respawn:/usr/bin/fruitjam-uart-login' "$inittab_src"
if grep -q 'ttyAMA0::respawn:/usr/bin/hush' "$inittab_src"; then
	echo "ttyAMA0 hush respawn loop was reintroduced" >&2
	exit 1
fi
grep -q '/etc/profile.d/\*.sh' "$profile_src"
grep -q 'clear()' "$cls_src"
grep -q 'cls()' "$cls_src"
python3 - "$web_cgi_src" "$airlift_src" <<'PY'
import sys
from pathlib import Path

cgi = Path(sys.argv[1]).read_text()
airlift = Path(sys.argv[2]).read_text()

for source, name in ((cgi, "CGI"), (airlift, "AirLift")):
    if "LINUX_REBOOT_CMD_RESTART2" not in source or 'SYS_reboot' not in source:
        raise SystemExit(f"{name} BOOTSEL path does not use direct restart2")
    if 'fruitjamctl", "bootsel"' in source:
        raise SystemExit(f"{name} BOOTSEL path reintroduced fruitjamctl exec")
    if '\\"verified\\":false' not in source or "picotool info -a" not in source:
        raise SystemExit(f"{name} BOOTSEL response does not state host verification is required")
print("ok web bootsel direct restart guard")
PY
echo "ok uart login guard"
echo "ok clear cls profile guard"

echo "== no-mmu network source guards =="
python3 - "$telnetd_src" "$wget_src" "$services_src" "$httpd_src" "$airlift_src" <<'PY'
import sys
from pathlib import Path

telnetd = Path(sys.argv[1]).read_text()
wget = Path(sys.argv[2]).read_text()
services = Path(sys.argv[3]).read_text()
httpd = Path(sys.argv[4]).read_text()
airlift = Path(sys.argv[5]).read_text()

telnet_needles = {
    "SIGCHLD ignored": "signal(SIGCHLD, SIG_IGN)",
    "client TCP_NODELAY": "TCP_NODELAY",
    "client idle timeout": "SO_RCVTIMEO",
    "listener backlog": "listen(srv, 2)",
    "no wait in accept loop": "wait(",
}
for label, needle in telnet_needles.items():
    found = needle in telnetd
    if label == "no wait in accept loop":
        if found:
            raise SystemExit("fruitjam-telnetd reintroduced wait()")
    elif not found:
        raise SystemExit(f"fruitjam-telnetd missing {label}")

wget_needles = {
    "nonblocking sockets": "O_NONBLOCK",
    "bounded select loop": "select(",
    "connect timeout": "WGET_IO_TIMEOUT_MS",
    "receive timeout": "SO_RCVTIMEO",
    "send timeout": "SO_SNDTIMEO",
    "content length handling": "Content-Length:",
    "localhost support": "localhost",
}
for label, needle in wget_needles.items():
    if needle not in wget:
        raise SystemExit(f"fruitjam-wget missing {label}")

start_all = services.split("static int start_all(void)", 1)[1].split("\n}", 1)[0]
if "start_core()" in start_all:
    raise SystemExit("fruitjam-services all starts the large loopback core by default")
if "start_airlift_background()" not in start_all:
    raise SystemExit("fruitjam-services all does not start direct AirLift service in the background")
if '"airlift-monitor"' not in services or "fruitjam-airlift-monitor.pid" not in services:
    raise SystemExit("fruitjam-services missing AirLift inbound monitor/pidfile guard")
if '"airlift-monitor", NULL' not in services:
    raise SystemExit("fruitjam-services default AirLift worker does not use the monitor")
if "spawn_wait_airlift_serve(serve)" not in services:
    raise SystemExit("fruitjam-services AirLift monitor does not use heartbeat-aware wait")
if "pid_is_alive" not in services or "request_service_stop" not in services:
    raise SystemExit("fruitjam-services AirLift monitor missing stale pid/stop handling")
if "unlink(airlift_monitor_pid)" not in services:
    raise SystemExit("fruitjam-services AirLift monitor does not clean its pidfile")
if '"/usr/sbin/httpd"' in services:
    raise SystemExit("fruitjam-services httpd uses the large BusyBox httpd")
if '"/usr/sbin/fruitjam-httpd"' not in services:
    raise SystemExit("fruitjam-services httpd does not use the tiny loopback server")
if "configure_loopback()" not in services:
    raise SystemExit("fruitjam-services still relies on an external loopback applet")
if "sd_web_root" not in services or "Fruit Jam stuff" not in services:
    raise SystemExit("fruitjam-services missing SD web placeholder setup")
if "access(sd_index_path, F_OK)" not in services:
    raise SystemExit("fruitjam-services would overwrite user SD index.html")
if "INADDR_LOOPBACK" not in httpd or "/www/cgi-bin/fruitjam.cgi" not in httpd:
    raise SystemExit("fruitjam-httpd missing loopback CGI support")
if "SD_WEB_ROOT" not in httpd or "PLAYGROUND_PREFIX" not in httpd:
    raise SystemExit("fruitjam-httpd missing SD-root /playground split")
if "EADDRINUSE" not in httpd:
    raise SystemExit("fruitjam-httpd repeated starts are not idempotent")
if "#define AIRLIFT_WIFI_JOIN_POLLS 1500" not in airlift:
    raise SystemExit("airliftctl WiFi join timeout regressed below the boot-safe window")
if "SD_WEB_ROOT" not in airlift or "PLAYGROUND_PREFIX" not in airlift:
    raise SystemExit("airliftctl HTTP missing SD-root /playground split")
if "pio-packet-io" not in airlift or "boot-protocol-keyboard" not in airlift:
    raise SystemExit("airliftctl USB API missing explicit HID bridge status fields")
if "http_usbhost_bus_reset" not in airlift or "reset_ms" not in airlift:
    raise SystemExit("airliftctl USB API missing USB bus reset path")
for needle, label in [
    ("AIRLIFT_LOCK_PATH", "AirLift lock path"),
    ("O_WRONLY | O_CREAT | O_EXCL", "atomic PID lock create"),
    ("read_lock_owner", "PID lock owner read"),
    ("lock_owner_alive", "PID lock owner liveness check"),
    ("unlink(AIRLIFT_LOCK_PATH)", "stale lock cleanup"),
    ("atexit(airlift_lock_release)", "normal lock release"),
]:
    if needle not in airlift:
        raise SystemExit(f"airliftctl missing {label}")
if "AIRLIFT_START_LOG" not in airlift or "print_cached_airlift_info" not in airlift:
    raise SystemExit("airliftctl missing cached read-only AirLift info fallback")
if "AIRLIFT_HEARTBEAT_PATH" not in airlift or "inbound_heartbeat_poll" not in airlift:
    raise SystemExit("airliftctl missing inbound heartbeat updates")
if '"mqtt-sub"' not in airlift or "mqtt_subscribe(" not in airlift or "mqtt_send_subscribe" not in airlift:
    raise SystemExit("airliftctl missing MQTT subscribe path")
if "#include <stdbool.h>" not in airlift:
    raise SystemExit("airliftctl must include stdbool.h for MQTT subscribe verbose flag")
if '"mqtt-pub"' not in airlift or "USERNAME PASSWORD" not in airlift:
    raise SystemExit("airliftctl MQTT publish auth help regressed")
for needle, label in [
    ("airlift_heartbeat_path", "AirLift heartbeat path"),
    ("spawn_wait_airlift_serve", "AirLift heartbeat-aware wait"),
    ("WNOHANG", "nonblocking AirLift monitor wait"),
    ("airlift_heartbeat_stale_sec", "AirLift stale heartbeat window"),
    ("AirLift inbound heartbeat stale; restarting", "AirLift stale restart log"),
    ("airlift-heartbeat age=", "AirLift heartbeat status output"),
]:
    if needle not in services:
        raise SystemExit(f"fruitjam-services missing {label}")

print("ok telnetd no-mmu guard")
print("ok wget no-mmu guard")
print("ok services default guard")
print("ok sd web placeholder guard")
print("ok loopback httpd guard")
print("ok sd web route guard")
print("ok airlift join guard")
print("ok airlift lock guard")
print("ok airlift cached info guard")
print("ok airlift heartbeat guard")
print("ok airlift mqtt subscribe guard")
PY

echo "== audio and dvi helper syntax =="
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$mosquitto_pub_src"
echo "ok c syntax $mosquitto_pub_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$mosquitto_sub_src"
echo "ok c syntax $mosquitto_sub_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$telnetd_src"
echo "ok c syntax $telnetd_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$wget_src"
echo "ok c syntax $wget_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$httpd_src"
echo "ok c syntax $httpd_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$uart_login_src"
echo "ok c syntax $uart_login_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$mem_src"
echo "ok c syntax $mem_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$buttons_src"
echo "ok c syntax $buttons_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$dvi_src"
echo "ok c syntax $dvi_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$rtttl_src_c"
echo "ok c syntax $rtttl_src_c"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$wavplay_src"
echo "ok c syntax $wavplay_src"

echo "== shell syntax =="
find "$shell_src" -type f -name '*.sh' -print | sort | while IFS= read -r file; do
	sh -n "$file"
	if grep -q '^set -eu' "$file"; then
		echo "$file uses set -eu, which target hush rejects" >&2
		exit 1
	fi
	if grep -q '\$(' "$file"; then
		echo "$file uses \$(), which target hush rejects" >&2
		exit 1
	fi
	echo "ok $file"
done

echo "== rtttl shell syntax =="
find "$rtttl_src" -type f -name '*.sh' -print | sort | while IFS= read -r file; do
	sh -n "$file"
	if grep -q '^set -eu' "$file"; then
		echo "$file uses set -eu, which target hush rejects" >&2
		exit 1
	fi
	if grep -q '\$(' "$file"; then
		echo "$file uses \$(), which target hush rejects" >&2
		exit 1
	fi
	echo "ok $file"
done

echo "== rtttl wrapper sync =="
find "$rtttl_src" -type f -name '*.rtttl' -print | sort | while IFS= read -r file; do
	wrapper="${file%.rtttl}.sh"
	name=$(basename "$file")
	test -f "$wrapper"
	grep -F "exec /usr/bin/fruitjam-rtttl /root/rtttl/$name" "$wrapper" >/dev/null
	echo "ok $wrapper"
done

echo "== rtttl tune parse =="
find "$rtttl_src" -type f -name '*.rtttl' -print | sort | while IFS= read -r file; do
	out="$tmp/$(basename "$file" .rtttl).wav"
	python3 "$repo/scripts/fruitjam-audio-mic-test.py" \
		--make-sample-wav "$out" \
		--rtttl "$file" >/dev/null
	test -s "$out"
	echo "ok $file"
done

echo "== berry finite examples =="
"$host_berry" -m "$tmp/berry" "$tmp/berry/run-all.be"

echo "== berry fake hardware module =="
"$host_berry" -m "$tmp/berry" "$tmp/berry/07-fruitjam-module-fakefs.be"

echo "== berry visual examples =="
"$host_berry" -m "$tmp/berry" "$tmp/berry/run-visual.be"

echo "Fruit Jam example validation: ok"
