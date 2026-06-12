#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
berry_src="$repo/board/adafruit/adafruit_fruit_jam_rp2350/rootfs_overlay/root/berry"
shell_src="$repo/board/adafruit/adafruit_fruit_jam_rp2350/rootfs_overlay/root/sh"
rtttl_src="$repo/board/adafruit/adafruit_fruit_jam_rp2350/rootfs_overlay/root/rtttl"
host_berry="$repo/buildroot/dl/berry/git/berry"
web_cgi_src="$repo/package/fruitjam-utils/src/fruitjam-web-cgi.c"
berry_json_src="$repo/package/fruitjam-utils/src/fruitjam-berry-json.c"
web_page_src="$repo/board/adafruit/adafruit_fruit_jam_rp2350/rootfs_overlay/www/index.html"
dvi_src="$repo/package/fruitjam-utils/src/fruitjam-dvi.c"
rtttl_src_c="$repo/package/fruitjam-utils/src/fruitjam-rtttl.c"
wavplay_src="$repo/package/fruitjam-utils/src/fruitjam-wavplay.c"
telnetd_src="$repo/package/fruitjam-utils/src/fruitjam-telnetd.c"
wget_src="$repo/package/fruitjam-utils/src/fruitjam-wget.c"
httpd_src="$repo/package/fruitjam-utils/src/fruitjam-httpd.c"
services_src="$repo/package/fruitjam-utils/src/fruitjam-services.c"
airlift_src="$repo/package/fruitjam-airlift/src/airliftctl.c"

if [ ! -x "$host_berry" ]; then
	echo "validate-fruitjam-examples: missing host Berry at $host_berry" >&2
	exit 1
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fruitjam-examples.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cp -R "$berry_src" "$tmp/berry"

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

echo "== berry whitelist sync =="
python3 - "$berry_src" "$web_cgi_src" "$berry_json_src" <<'PY'
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
cgi_usbhost_cmds = {"status", "on", "off"}
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
for required_usbhost in ("status", "on", "off"):
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
if query == "action=berry-run&script=../../bad.be" and data.get("ok") is not False:
    raise SystemExit("bad berry-run path was not rejected")
PY
	echo "ok cgi $query"
}

echo "== web cgi host JSON =="
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-o "$tmp/fruitjam-web.cgi" "$web_cgi_src"
check_cgi_json "action=status"
check_cgi_json "action=i2c"
check_cgi_json "action=usbhost"
check_cgi_json "action=berry-list"
check_cgi_json "action=wav-list"
check_cgi_json "action=neopixels"
check_cgi_json "action=berry-run&script=../../bad.be"

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
if '"/usr/sbin/httpd"' in services:
    raise SystemExit("fruitjam-services httpd uses the large BusyBox httpd")
if '"/usr/sbin/fruitjam-httpd"' not in services:
    raise SystemExit("fruitjam-services httpd does not use the tiny loopback server")
if "configure_loopback()" not in services:
    raise SystemExit("fruitjam-services still relies on an external loopback applet")
if "INADDR_LOOPBACK" not in httpd or "/www/cgi-bin/fruitjam.cgi" not in httpd:
    raise SystemExit("fruitjam-httpd missing loopback CGI support")
if "EADDRINUSE" not in httpd:
    raise SystemExit("fruitjam-httpd repeated starts are not idempotent")
if "#define AIRLIFT_WIFI_JOIN_POLLS 1500" not in airlift:
    raise SystemExit("airliftctl WiFi join timeout regressed below the boot-safe window")

print("ok telnetd no-mmu guard")
print("ok wget no-mmu guard")
print("ok services default guard")
print("ok loopback httpd guard")
print("ok airlift join guard")
PY

echo "== audio and dvi helper syntax =="
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$telnetd_src"
echo "ok c syntax $telnetd_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$wget_src"
echo "ok c syntax $wget_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$httpd_src"
echo "ok c syntax $httpd_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$dvi_src"
echo "ok c syntax $dvi_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$rtttl_src_c"
echo "ok c syntax $rtttl_src_c"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$wavplay_src"
echo "ok c syntax $wavplay_src"

echo "== shell syntax =="
find "$shell_src" -type f -name '*.sh' -print | sort | while IFS= read -r file; do
	sh -n "$file"
	echo "ok $file"
done

echo "== rtttl shell syntax =="
find "$rtttl_src" -type f -name '*.sh' -print | sort | while IFS= read -r file; do
	sh -n "$file"
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

echo "== berry visual examples =="
"$host_berry" -m "$tmp/berry" "$tmp/berry/run-visual.be"

echo "Fruit Jam example validation: ok"
