#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "${WILI8JAM_ROOT+x}" ]; then
	wili8jam_root=$WILI8JAM_ROOT
else
	wili8jam_root="$repo/../wili8jam"
fi
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
fruitjam_shell_src="$repo/package/fruitjam-utils/src/fruitjam-shell.c"
ftpd_src="$repo/package/fruitjam-utils/src/fruitjam-ftpd.c"
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
uptime_src="$repo/package/fruitjam-utils/src/fruitjam-uptime.c"
du_src="$repo/package/fruitjam-utils/src/fruitjam-du.c"
ps_src="$repo/package/fruitjam-utils/src/fruitjam-ps.c"
pgrep_src="$repo/package/fruitjam-utils/src/fruitjam-pgrep.c"
cdc_smoke_src="$repo/scripts/cdc-smoke-test.py"
usb_keyboard_smoke_src="$repo/scripts/usbhost-keyboard-smoke.py"
usbhost_hcd_smoke_src="$repo/scripts/usbhost-hcd-smoke.py"
wili8jam_usb_compare_src="$repo/scripts/compare-wili8jam-usb-config.py"
wili8jam_media_compare_src="$repo/scripts/compare-wili8jam-media-config.py"
mqtt_smoke_src="$repo/scripts/mqtt-smoke-test.py"
recover_flash_src="$repo/scripts/fruitjam-recover-flash.py"
validate_image_src="$repo/scripts/validate-fruitjam-image.sh"
defconfig_src="$repo/configs/adafruit_fruit_jam_rp2350_defconfig"
bootloader_main_src="$repo/package/pico2-bootloader/bootloader/src/main.c"
bootloader_clocks_src="$repo/package/pico2-bootloader/bootloader/src/clocks.h"
kernel_bootsel_restart_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0013-riscv-rp2350-add-bootsel-restart-command.patch"
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
kernel_cdc_bootsel_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0093-usb-gadget-acm-add-rp2350-bootsel-touch.patch"
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
kernel_usbhost_ack_sweep_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0095-misc-sweep-fruitjam-usbhost-hcd-ack-arm-gap.patch"
kernel_usbhost_upstream_hcd_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0096-misc-use-upstream-style-hcd-setup-probe.patch"
kernel_usbhost_upstream_status_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0097-misc-use-upstream-style-hcd-status-out.patch"
kernel_usbhost_interrupt_out_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0098-misc-add-fruitjam-hcd-interrupt-out.patch"
kernel_usbhost_transfer_types_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0099-misc-expand-fruitjam-hcd-transfer-types.patch"
kernel_usbhost_no_data_control_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0100-misc-sweep-fruitjam-hcd-no-data-control.patch"
kernel_usbhost_interrupt_idle_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0101-misc-treat-fruitjam-interrupt-in-nak-as-idle.patch"
kernel_usbhost_xinput_tx_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0102-misc-size-fruitjam-usbhost-tx-buffer-for-xinput.patch"
kernel_usbhost_pre_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0103-misc-add-fruitjam-hcd-pre-for-low-speed-hub-devices.patch"
kernel_usbhost_pre_finish_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0104-misc-finish-fruitjam-hcd-pre-program-selection.patch"
kernel_usbhost_pre_stall_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0105-misc-wait-for-fruitjam-hcd-pre-tx-idle.patch"
kernel_usbhost_wili8jam_defaults_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0113-misc-default-fruitjam-usbhost-to-wili8jam-config.patch"
kernel_usbhost_rx_quiesce_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0114-misc-quiesce-fruitjam-usbhost-rx-after-receive.patch"
kernel_usbhost_manual_hcd_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0115-misc-add-fruitjam-usbhost-manual-hcd-start.patch"
kernel_usbhost_reset_settle_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0116-misc-settle-fruitjam-hcd-port-reset-before-ep0.patch"
kernel_usbhost_reset_settle_config_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0123-misc-make-fruitjam-hcd-reset-settle-configurable.patch"
kernel_usbhost_reset_settle_status_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0124-misc-report-fruitjam-hcd-reset-settle-status-value.patch"
kernel_usbhost_prestart_power_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0125-misc-power-cycle-fruitjam-hcd-before-registration.patch"
kernel_usbhost_auto_recover_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0126-misc-auto-recover-fruitjam-hcd-ep0-faults.patch"
kernel_usbhost_reset_sof_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0127-misc-send-fruitjam-hcd-sof-after-reset-settle.patch"
kernel_usbhost_status_busy_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0128-misc-keep-fruitjam-usbhost-status-nonblocking.patch"
kernel_usbhost_auto_recover_cap_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0129-misc-cap-fruitjam-hcd-ep0-auto-recovery.patch"
kernel_usbhost_data_ack_timing_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0130-misc-ack-fruitjam-hcd-data-with-pico-timing.patch"
kernel_usbhost_hcd_in_pico_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0131-misc-use-pico-rx-lifecycle-for-fruitjam-hcd-in.patch"
kernel_usbhost_hcd_in_irq_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0132-misc-mask-irqs-for-fruitjam-hcd-in-window.patch"
kernel_usbhost_hcd_in_irq_bound_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0133-misc-bound-fruitjam-hcd-in-irq-window.patch"
kernel_usbhost_fault_summary_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0117-misc-preserve-fruitjam-hcd-fault-stage-summary.patch"
kernel_usbhost_pico_rx_lifecycle_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0118-misc-use-pico-rx-lifecycle-for-fruitjam-hcd-setup.patch"
kernel_usbhost_pico_wait_bounds_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0119-misc-match-pico-pio-usb-handshake-wait-bounds.patch"
kernel_dvi_wili_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0120-misc-add-wili8jam-rgb565-dvi-mode.patch"
kernel_usbhost_eop_alive_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0121-misc-keep-fruitjam-hcd-eop-alive-after-rx.patch"
kernel_audio_waveform_patch="$repo/board/raspberrypi/raspberrypi-pico2/patches/linux/0122-misc-add-wili8jam-audio-waveforms.patch"
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
python3 - "$tmp/fruitjam-shell" "$tmp" <<'PY'
import errno
import fcntl
import os
import pty
import subprocess
import sys
import time

exe = sys.argv[1]
work = sys.argv[2]
path_dir = os.path.join(work, "shelltab")
os.makedirs(path_dir, exist_ok=True)
with open(os.path.join(path_dir, "path-ok.txt"), "w", encoding="utf-8") as f:
    f.write("PATH_TAB_OK\n")

master, slave = pty.openpty()
proc = subprocess.Popen(
    [exe],
    stdin=slave,
    stdout=slave,
    stderr=slave,
    close_fds=True,
    cwd=work,
)
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
os.write(master, b"cat she\tpa\t\r")
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
    raise SystemExit("fruitjam-shell command tab completion failed")
if "cat shelltab/path-ok.txt" not in text or "PATH_TAB_OK" not in text:
    raise SystemExit("fruitjam-shell path tab completion failed")
if text.count("HIST_OK") < 2:
    raise SystemExit("fruitjam-shell history recall failed")
if "history" not in text or "echo HIST_OK" not in text:
    raise SystemExit("fruitjam-shell history builtin failed")
print("ok fruitjam-shell line editing")
PY

echo "== ftp daemon host behavior =="
ftp_root="$tmp/ftp-root"
mkdir -p "$ftp_root"
printf 'hello ftp\n' > "$ftp_root/hello.txt"
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-DFTP_ROOT="\"$ftp_root\"" \
	-o "$tmp/fruitjam-ftpd" "$ftpd_src"
python3 - "$tmp/fruitjam-ftpd" "$ftp_root" <<'PY'
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

exe = sys.argv[1]
root = Path(sys.argv[2])

def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port

port = free_port()
proc = subprocess.Popen([exe, str(port)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

def connect_control():
    deadline = time.time() + 3
    last = None
    while time.time() < deadline:
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=1)
        except OSError as exc:
            last = exc
            time.sleep(0.05)
    raise RuntimeError(f"ftp control did not open: {last}")

def read_reply(ctrl):
    ctrl.settimeout(3)
    data = bytearray()
    while True:
        chunk = ctrl.recv(1)
        if not chunk:
            raise RuntimeError("ftp control closed")
        data.extend(chunk)
        if data.endswith(b"\r\n"):
            text = data.decode("utf-8", "replace")
            if len(text) >= 4 and text[3] == "-":
                code = text[:3] + " "
                while code not in text.splitlines()[-1]:
                    line = bytearray()
                    while True:
                        chunk = ctrl.recv(1)
                        if not chunk:
                            raise RuntimeError("ftp control closed in multiline reply")
                        line.extend(chunk)
                        if line.endswith(b"\r\n"):
                            break
                    text += line.decode("utf-8", "replace")
            return text

def send(ctrl, command):
    ctrl.sendall((command + "\r\n").encode())
    return read_reply(ctrl)

def assert_code(reply, code):
    if not reply.startswith(str(code) + " ") and not reply.startswith(str(code) + "-"):
        raise RuntimeError(f"expected FTP {code}, got {reply!r}")

def active_listener():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    s.listen(1)
    return s, s.getsockname()[1]

def port_arg(port):
    return "127,0,0,1,%u,%u" % (port >> 8, port & 0xff)

ctrl = None
try:
    ctrl = connect_control()
    assert_code(read_reply(ctrl), 220)
    assert_code(send(ctrl, "USER anonymous"), 331)
    assert_code(send(ctrl, "PASS x"), 230)
    feat = send(ctrl, "FEAT")
    for marker in ("EPSV", "PASV", "EPRT", "PORT", "SIZE", "MDTM"):
        if marker not in feat:
            raise RuntimeError(f"FEAT missing {marker}: {feat!r}")

    epsv = send(ctrl, "EPSV")
    assert_code(epsv, 229)
    match = re.search(r"\(\|\|\|([0-9]+)\|\)", epsv)
    if not match:
        raise RuntimeError(f"bad EPSV reply: {epsv!r}")
    data = socket.create_connection(("127.0.0.1", int(match.group(1))), timeout=3)
    assert_code(send(ctrl, "LIST"), 150)
    listing = data.recv(4096).decode("utf-8", "replace")
    data.close()
    assert_code(read_reply(ctrl), 226)
    if "hello.txt" not in listing:
        raise RuntimeError(f"passive LIST missing hello.txt: {listing!r}")

    listener, active_port = active_listener()
    assert_code(send(ctrl, "PORT " + port_arg(active_port)), 200)
    ctrl.sendall(b"STOR active.txt\r\n")
    assert_code(read_reply(ctrl), 150)
    conn, _ = listener.accept()
    conn.sendall(b"active-ok")
    conn.close()
    listener.close()
    assert_code(read_reply(ctrl), 226)
    if (root / "active.txt").read_text() != "active-ok":
        raise RuntimeError("active STOR wrote wrong data")

    listener, active_port = active_listener()
    assert_code(send(ctrl, "PORT " + port_arg(active_port)), 200)
    ctrl.sendall(b"APPE active.txt\r\n")
    assert_code(read_reply(ctrl), 150)
    conn, _ = listener.accept()
    conn.sendall(b"-append")
    conn.close()
    listener.close()
    assert_code(read_reply(ctrl), 226)

    listener, active_port = active_listener()
    assert_code(send(ctrl, f"EPRT |1|127.0.0.1|{active_port}|"), 200)
    ctrl.sendall(b"RETR active.txt\r\n")
    assert_code(read_reply(ctrl), 150)
    conn, _ = listener.accept()
    got = bytearray()
    while True:
        chunk = conn.recv(4096)
        if not chunk:
            break
        got.extend(chunk)
    conn.close()
    listener.close()
    assert_code(read_reply(ctrl), 226)
    if got != b"active-ok-append":
        raise RuntimeError(f"active RETR got {got!r}")

    assert_code(send(ctrl, "RNFR active.txt"), 350)
    assert_code(send(ctrl, "RNTO renamed.txt"), 250)
    assert_code(send(ctrl, "SIZE renamed.txt"), 213)
    assert_code(send(ctrl, "MKD tempdir"), 257)
    assert_code(send(ctrl, "RMD tempdir"), 250)
    assert_code(send(ctrl, "DELE renamed.txt"), 250)
    assert_code(send(ctrl, "QUIT"), 221)
finally:
    if ctrl is not None:
        ctrl.close()
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)

print("ok fruitjam-ftpd passive and active transfers")
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
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-DPROC_ROOT="\"$proc_root\"" \
	-o "$tmp/fruitjam-uptime" "$uptime_src"
"$tmp/fruitjam-mem" > "$tmp/fruitjam-mem.txt"
"$tmp/fruitjam-mem" json > "$tmp/fruitjam-mem.json"
"$tmp/fruitjam-uptime" > "$tmp/fruitjam-uptime.txt"
"$tmp/fruitjam-uptime" json > "$tmp/fruitjam-uptime.json"
python3 - "$tmp/fruitjam-mem.txt" "$tmp/fruitjam-mem.json" "$tmp/fruitjam-uptime.txt" "$tmp/fruitjam-uptime.json" <<'PY'
import json
import sys
from pathlib import Path

text = Path(sys.argv[1]).read_text()
data = json.loads(Path(sys.argv[2]).read_text())
uptime_text = Path(sys.argv[3]).read_text()
uptime_data = json.loads(Path(sys.argv[4]).read_text())
if "Fruit Jam memory" not in text or "Note: no-MMU" not in text:
    raise SystemExit("fruitjam-mem text output missing expected labels")
if data.get("unit") != "kB" or data.get("mem", {}).get("total") != 8192:
    raise SystemExit("fruitjam-mem json missing memory totals")
if data.get("mem", {}).get("cache") != 1088:
    raise SystemExit("fruitjam-mem json cache calculation regressed")
if data.get("uptime_seconds") != 12.34 or data.get("load_15m") != 0.03:
    raise SystemExit("fruitjam-mem json uptime/load parse regressed")
if "up 00:00, load average: 0.01, 0.02, 0.03" not in uptime_text:
    raise SystemExit(f"fruitjam-uptime text regressed: {uptime_text!r}")
if uptime_data.get("uptime_seconds") != 12 or uptime_data.get("idle_seconds") != 56:
    raise SystemExit("fruitjam-uptime json seconds regressed")
if uptime_data.get("load_5m") != 0.02 or uptime_data.get("load_15m") != 0.03:
    raise SystemExit("fruitjam-uptime json load regressed")
PY
echo "ok fruitjam-mem text"
echo "ok fruitjam-mem json"
echo "ok fruitjam-uptime text"
echo "ok fruitjam-uptime json"

echo "== disk usage helper host behavior =="
du_root="$tmp/du"
mkdir -p "$du_root/sub"
printf 'hello\n' > "$du_root/a.txt"
printf 'berry\n' > "$du_root/sub/b.be"
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-o "$tmp/fruitjam-du" "$du_src"
"$tmp/fruitjam-du" -s "$du_root" > "$tmp/fruitjam-du-summary.txt"
"$tmp/fruitjam-du" -a "$du_root" > "$tmp/fruitjam-du-all.txt"
"$tmp/fruitjam-du" -b json "$du_root" > "$tmp/fruitjam-du.json"
python3 - "$tmp/fruitjam-du-summary.txt" "$tmp/fruitjam-du-all.txt" "$tmp/fruitjam-du.json" "$du_root" <<'PY'
import json
import sys
from pathlib import Path

summary = Path(sys.argv[1]).read_text()
all_entries = Path(sys.argv[2]).read_text()
data = json.loads(Path(sys.argv[3]).read_text())
root = sys.argv[4]
if root not in summary:
    raise SystemExit("fruitjam-du summary missing root path")
if f"{root}/a.txt" not in all_entries or f"{root}/sub/b.be" not in all_entries:
    raise SystemExit("fruitjam-du -a missing file entries")
paths = {entry.get("path"): entry for entry in data.get("entries", [])}
if root not in paths:
    raise SystemExit("fruitjam-du json missing root entry")
if paths[root].get("bytes", 0) <= 0:
    raise SystemExit("fruitjam-du json root size not positive")
if data.get("unit") != "bytes":
    raise SystemExit("fruitjam-du json unit regressed")
PY
echo "ok fruitjam-du text"
echo "ok fruitjam-du json"

echo "== process helper host behavior =="
proc_root="$tmp/proc"
mkdir -p "$proc_root/1" "$proc_root/42"
printf '1 (init) S 0 1 1 0 -1 4194560 10 0 0 0 1 2 0 0 20 0 1 0 100 409600 12 0 0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0\n' > "$proc_root/1/stat"
printf '/sbin/init\0' > "$proc_root/1/cmdline"
printf '42 (fruitjam shell) R 1 1 1 0 -1 4194560 20 0 0 0 3 4 0 0 20 0 1 0 200 819200 8 0 0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0\n' > "$proc_root/42/stat"
: > "$proc_root/42/cmdline"
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-DPROC_ROOT="\"$proc_root\"" \
	-o "$tmp/fruitjam-ps" "$ps_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os \
	-DPROC_ROOT="\"$proc_root\"" -DFRUITJAM_PGREP_DRY_RUN \
	-o "$tmp/fruitjam-pgrep" "$pgrep_src"
ln -sf fruitjam-pgrep "$tmp/pgrep"
ln -sf fruitjam-pgrep "$tmp/pkill"
"$tmp/fruitjam-ps" > "$tmp/fruitjam-ps.txt"
"$tmp/fruitjam-ps" json > "$tmp/fruitjam-ps.json"
"$tmp/fruitjam-pgrep" init > "$tmp/fruitjam-pgrep-init.txt"
"$tmp/pgrep" -f /sbin/init > "$tmp/fruitjam-pgrep-full.txt"
"$tmp/pgrep" -l shell > "$tmp/fruitjam-pgrep-list.txt"
"$tmp/pkill" -9 -x "fruitjam shell" > "$tmp/fruitjam-pkill.txt"
if "$tmp/pgrep" missing-process > "$tmp/fruitjam-pgrep-missing.txt"; then
	echo "fruitjam-pgrep matched a missing process" >&2
	exit 1
fi
python3 - "$tmp/fruitjam-ps.txt" "$tmp/fruitjam-ps.json" "$tmp/fruitjam-pgrep-init.txt" "$tmp/fruitjam-pgrep-full.txt" "$tmp/fruitjam-pgrep-list.txt" "$tmp/fruitjam-pkill.txt" <<'PY'
import json
import sys
from pathlib import Path

text = Path(sys.argv[1]).read_text()
data = json.loads(Path(sys.argv[2]).read_text())
pgrep_init = Path(sys.argv[3]).read_text().strip()
pgrep_full = Path(sys.argv[4]).read_text().strip()
pgrep_list = Path(sys.argv[5]).read_text().strip()
pkill = Path(sys.argv[6]).read_text().strip()
if "PID" not in text or "PPID" not in text or "COMMAND" not in text:
    raise SystemExit("fruitjam-ps text header missing")
if "/sbin/init" not in text or "[fruitjam shell]" not in text:
    raise SystemExit("fruitjam-ps text process output missing")
procs = {proc["pid"]: proc for proc in data.get("processes", [])}
if procs.get(1, {}).get("command") != "/sbin/init":
    raise SystemExit("fruitjam-ps json missing init command")
if procs.get(42, {}).get("command") != "[fruitjam shell]":
    raise SystemExit("fruitjam-ps json missing comm fallback")
if procs.get(42, {}).get("state") != "R" or procs.get(42, {}).get("ppid") != 1:
    raise SystemExit("fruitjam-ps json state/ppid regressed")
if procs.get(1, {}).get("vsize_kb") != 400:
    raise SystemExit("fruitjam-ps json vsize calculation regressed")
if pgrep_init != "1" or pgrep_full != "1":
    raise SystemExit(f"fruitjam-pgrep did not find init: {pgrep_init!r} {pgrep_full!r}")
if pgrep_list != "42 fruitjam shell":
    raise SystemExit(f"fruitjam-pgrep -l did not print command: {pgrep_list!r}")
if pkill != "42 9":
    raise SystemExit(f"fruitjam-pkill dry run did not select signal target: {pkill!r}")
PY
echo "ok fruitjam-ps text"
echo "ok fruitjam-ps json"
echo "ok fruitjam-pgrep"
echo "ok fruitjam-pkill"

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
    path_dir = os.path.join(tmp, "usbkbdtab")
    os.makedirs(path_dir, exist_ok=True)
    with open(os.path.join(path_dir, "path-ok.txt"), "w", encoding="utf-8") as f:
        f.write("USBKBD_PATH_OK\n")

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
        [exe, "kbd-shell", "5"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=tmp,
    )
    keys = []
    letters = {chr(ord("a") + i): 4 + i for i in range(26)}
    digits = {str(i): 29 + i for i in range(1, 10)}
    digits["0"] = 39
    punctuation = {" ": 44, "\n": 40, "\t": 43, "/": 56, "-": 45, ".": 55}

    def add_text(text):
        for ch in text:
            if ch in letters:
                keys.append(letters[ch])
            elif ch in digits:
                keys.append(digits[ch])
            elif ch in punctuation:
                keys.append(punctuation[ch])
            else:
                raise SystemExit(f"test cannot type {ch!r}")

    add_text("cat usbkbdtab/path")
    keys.append(43)
    keys.append(40)
    add_text("echo ok\n")
    add_text("his")
    keys.append(43)
    keys.append(40)
    keys.append(82)
    keys.append(40)
    add_text("exit\n")
    reports = []
    for key in keys:
        reports.append(report_for_key(key))
        reports.append(release_report)
    seen = bytearray()
    answered = 0
    deadline = time.time() + 7
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
if "cat usbkbdtab/path-ok.txt" not in shell or "USBKBD_PATH_OK" not in shell:
    raise SystemExit(f"fruitjam-usbhost kbd-shell path tab completion failed: {shell!r} {shell_err!r}")
if "1 cat usbkbdtab/path-ok.txt" not in shell or shell.count("3 history") < 2:
    raise SystemExit(f"fruitjam-usbhost kbd-shell history recall failed: {shell!r} {shell_err!r}")
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
python3 - "$kernel_usbhost_patch" "$kernel_usbhost_pio_patch" "$kernel_usbhost_tx_patch" "$kernel_usbhost_rx_patch" "$kernel_usbhost_dma_patch" "$kernel_usbhost_reset_patch" "$kernel_usbhost_reloc_patch" "$kernel_usbhost_rx_osr_patch" "$kernel_usbhost_selfrx_patch" "$kernel_usbhost_eop_patch" "$kernel_usbhost_eop_reset_patch" "$kernel_usbhost_tx_latch_patch" "$kernel_usbhost_tx_idle_patch" "$kernel_usbhost_tx_eop_patch" "$kernel_usbhost_debug_patch" "$kernel_usbhost_debug_finish_patch" "$kernel_usbhost_gated_patch" "$kernel_usbhost_gated_write_patch" "$kernel_usbhost_dma_eop_patch" "$kernel_usbhost_dma_idle_patch" "$kernel_usbhost_rx_drain_patch" "$kernel_usbhost_setup_selfrx_patch" "$kernel_usbhost_rx_tail_patch" "$kernel_usbhost_cpu_tx_patch" "$kernel_usbhost_noeop_patch" "$kernel_usbhost_sweep_patch" "$kernel_usbhost_empty_eop_patch" "$kernel_usbhost_clock_diag_patch" "$kernel_usbhost_active_sof_patch" "$kernel_usbhost_combo_patch" "$kernel_usbhost_fast_patch" "$kernel_usbhost_tight_patch" "$kernel_usbhost_burst_patch" "$kernel_usbhost_stream_patch" "$kernel_usbhost_stream_wait_patch" "$kernel_usbhost_live_drain_patch" "$kernel_usbhost_low_speed_patch" "$kernel_usbhost_tx_eop_gated_patch" "$kernel_usbhost_combo_skipack_patch" "$kernel_usbhost_keyboard_target_patch" "$kernel_usbhost_ack_sweep_patch" "$kernel_usbhost_upstream_hcd_patch" "$kernel_usbhost_upstream_status_patch" "$kernel_usbhost_interrupt_out_patch" "$kernel_usbhost_transfer_types_patch" "$kernel_usbhost_no_data_control_patch" "$kernel_usbhost_interrupt_idle_patch" "$kernel_usbhost_xinput_tx_patch" "$kernel_usbhost_pre_patch" "$kernel_usbhost_pre_finish_patch" "$kernel_usbhost_wili8jam_defaults_patch" "$kernel_usbhost_rx_quiesce_patch" "$kernel_usbhost_pico_wait_bounds_patch" "$kernel_config_src" "$dts_src" "$usbhost_src" "$web_cgi_src" "$airlift_src" "$bootloader_clocks_src" "$web_page_src" <<'PY'
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
ack_sweep_patch = Path(sys.argv[41]).read_text()
upstream_hcd_patch = Path(sys.argv[42]).read_text()
upstream_status_patch = Path(sys.argv[43]).read_text()
interrupt_out_patch = Path(sys.argv[44]).read_text()
transfer_types_patch = Path(sys.argv[45]).read_text()
no_data_control_patch = Path(sys.argv[46]).read_text()
interrupt_idle_patch = Path(sys.argv[47]).read_text()
xinput_tx_patch = Path(sys.argv[48]).read_text()
pre_patch = Path(sys.argv[49]).read_text() + "\n" + Path(sys.argv[50]).read_text()
defaults_patch = Path(sys.argv[51]).read_text()
rx_quiesce_patch = Path(sys.argv[52]).read_text()
pico_wait_bounds_patch = Path(sys.argv[53]).read_text()
config = Path(sys.argv[54]).read_text()
dts = Path(sys.argv[55]).read_text()
helper = Path(sys.argv[56]).read_text()
cgi = Path(sys.argv[57]).read_text()
airlift = Path(sys.argv[58]).read_text()
clocks = Path(sys.argv[59]).read_text()
web = Path(sys.argv[60]).read_text()
if "CONFIG_FRUITJAM_USBHOST_BRIDGE" not in patch or "fruitjam_usbhost.c" not in patch:
    raise SystemExit("kernel patch missing Fruit Jam USB host bridge driver")
if "/dev/fruitjam-usbhost" not in patch or "pio-packet-io-pending" not in patch:
    raise SystemExit("kernel USB host bridge patch missing device/status contract")
for needle in (
    "FJ_USBHOST_PIO_INDEX_DEFAULT",
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
    "FJ_USBHOST_TX_DMA_CHANNEL_DEFAULT",
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
    "FJ_USBHOST_CLK_SYS_HZ_DEFAULT",
    "clk_sys_hz %u",
    "tx_clkdiv 0x%08x",
    "eop_clkdiv 0x%08x",
):
    if needle not in clock_diag_patch:
        raise SystemExit(f"kernel USB host clock diagnostics patch missing {needle}")
for needle in (
    "wili8jam electrical configuration",
    "FJ_USBHOST_PIO_INDEX_DEFAULT\t0u",
    "FJ_USBHOST_CLK_SYS_HZ_DEFAULT\t252000000u",
    "FJ_USBHOST_TX_DMA_CHANNEL_DEFAULT 9u",
):
    if needle not in defaults_patch:
        raise SystemExit(f"kernel USB host wili8jam defaults patch missing {needle}")
for needle in (
    "Pico-PIO-USB/wili8jam disables the RX state machine",
    "static void fj_usbhost_pio_record_rx(struct fruitjam_usbhost *uh, int ret",
    "static void fj_usbhost_pio_finish_receive",
    "Preserve the receive diagnostics first",
    "fj_usbhost_pio_record_rx(uh, ret, len);",
    "fj_usbhost_pio_disable_receive(uh);",
    "fj_usbhost_pio_finish_receive(uh, ret, len);",
):
    if needle not in rx_quiesce_patch:
        raise SystemExit(f"kernel USB host RX quiesce patch missing {needle}")
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
    "FJ_USBHOST_HCD_ACK_ARM_GAP_SWEEP_COUNT 8u",
    "fj_usbhost_hcd_ack_arm_gap_for_attempt",
    "4, 2, 1, 0, 8, 12, 16, 6",
    "hcd-control-read setup-arm-ack addr=%u len=%u attempt=%u gap=%u tx-len=%u",
):
    if needle not in ack_sweep_patch:
        raise SystemExit(f"kernel USB host HCD ACK-arm sweep patch missing {needle}")
for needle in (
    "fj_usbhost_pio_send_setup_data0_upstream_addr",
    "Match Pico-PIO-USB's host setup stage",
    "FJ_USBHOST_HCD_CONTROL_ATTEMPTS 9u",
    "mode=upstream",
    "mode=arm-gap",
    "hcd-control-read failed modes=upstream,arm-gap",
):
    if needle not in upstream_hcd_patch:
        raise SystemExit(f"kernel USB host upstream-style HCD patch missing {needle}")
for needle in (
    "fj_usbhost_hcd_control_status_out",
    "fj_usbhost_pio_control_status_out_arm_gap",
    "fj_usbhost_pio_send_data_packet(uh, FJ_USB_PID_DATA1, NULL, 0",
    "hcd-control-status-out failed upstream-ret=%d fallback-ret=%d",
    "Pico-PIO-USB handles OUT transactions",
):
    if needle not in upstream_status_patch:
        raise SystemExit(f"kernel USB host upstream-style status OUT patch missing {needle}")
for needle in (
    "fj_usbhost_hcd_out",
    "usb_gettoggle(udev, ep, 1)",
    "usb_dotoggle(udev, ep, 1)",
    "fj_usbhost_pio_send_data_packet(",
    "usb_pipeout(urb->pipe)",
    "hcd-interrupt",
    "hcd-bulk",
    "wili8jam enables TinyUSB XInput support",
):
    if needle not in interrupt_out_patch:
        raise SystemExit(f"kernel USB host interrupt OUT patch missing {needle}")
for needle in (
    "fj_usbhost_hcd_control_write",
    "fj_usbhost_hcd_control_data_out",
    "fj_usbhost_hcd_control_status_in",
    "fj_usbhost_hcd_in",
    "usb_gettoggle(udev, ep, 0)",
    "usb_dotoggle(udev, ep, 0)",
    "usb_pipebulk(urb->pipe)",
    "usb_pipein(urb->pipe)",
    "hcd-control-write",
    "hcd-bulk",
    "control write transfers with DATA1",
):
    if needle not in transfer_types_patch:
        raise SystemExit(f"kernel USB host transfer-types patch missing {needle}")
for needle in (
    "fj_usbhost_pio_control_no_data",
    "fj_usbhost_pio_send_setup_data0_upstream_addr",
    "fj_usbhost_hcd_ack_arm_gap_for_attempt",
    "FJ_USBHOST_HCD_CONTROL_ATTEMPTS",
    "mode=upstream",
    "mode=arm-gap",
    "setup-fail %s addr=%u request=0x%02x",
    "setup-ack-ok %s addr=%u request=0x%02x",
    "status-in-ok %s addr=%u request=0x%02x",
    "no-data failed modes=upstream,arm-gap",
    "SET_ADDRESS",
    "SET_CONFIGURATION",
    "SET_PROTOCOL",
    "SET_IDLE",
):
    if needle not in no_data_control_patch:
        raise SystemExit(f"kernel USB host no-data control patch missing {needle}")
for needle in (
    "idle_nak_success",
    "in-idle-nak",
    "FJ_USB_PID_NAK",
    "bool interrupt_in",
    "wili8jam/TinyUSB leaves such transfers live",
    "Keep bulk/control behavior unchanged",
):
    if needle not in interrupt_idle_patch:
        raise SystemExit(f"kernel USB host interrupt idle-NAK patch missing {needle}")
for needle in (
    "FJ_USBHOST_TX_ENCODED_MAX\t192u",
    "64-byte HID/XInput payload",
    "Xbox 360 wireless receiver init packets",
):
    if needle not in xinput_tx_patch:
        raise SystemExit(f"kernel USB host XInput TX buffer patch missing {needle}")
for needle in (
    "FJ_USB_PID_PRE",
    "hcd_need_pre",
    "fj_usbhost_pio_send_pre",
    "fj_usbhost_pio_restore_full_speed",
    "fj_usbhost_hcd_device_needs_pre",
    "USB_SPEED_LOW && udev->parent",
    "uh->hcd_need_pre ? 1 : FJ_USBHOST_HCD_CONTROL_ATTEMPTS",
    "wili8jam's known-working Fruit Jam USB host stack",
):
    if needle not in pre_patch:
        raise SystemExit(f"kernel USB host PRE patch missing {needle}")
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
if "clock-frequency = <252000000>" not in dts or "raspberrypi,clk-sys-hz = <252000000>" not in dts:
    raise SystemExit("Fruit Jam DTS must keep clk_sys at 252 MHz for wili8jam-style PIO USB timing")
if "#define PLL_SYS_HZ\t(252UL * MHZ)" not in clocks or "PLL_FBDIV_INT_REG] = 105" not in clocks:
    raise SystemExit("Fruit Jam bootloader must keep clk_sys at 252 MHz for wili8jam-style PIO USB timing")
for needle in ("0x50400000", "raspberrypi,pio = <2>", "raspberrypi,sm-tx = <0>",
               "raspberrypi,sm-rx = <1>", "raspberrypi,sm-eop = <2>",
               "0x50000000", "\"pio\", \"resets\", \"dma\"",
               "raspberrypi,tx-dma-channel = <9>"):
    if needle not in dts:
        raise SystemExit(f"Fruit Jam DTS missing USB host PIO2 setting {needle}")
for source, name in ((helper, "fruitjam-usbhost"), (cgi, "CGI"), (airlift, "AirLift")):
    if "/dev/fruitjam-usbhost" not in source:
        raise SystemExit(f"{name} does not use the kernel USB host bridge when present")
    if "pio-packet-io" not in source:
        raise SystemExit(f"{name} missing next PIO packet I/O milestone")
    if "pio_ready" not in source:
        raise SystemExit(f"{name} does not surface PIO readiness")
if "pio-init" not in helper or "tx-test" not in helper or "tx-test-cpu" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose PIO init/TX test")
if "self-rx" not in helper or "self-rx-cpu" not in helper:
    raise SystemExit("fruitjam-usbhost helper does not expose self-RX diagnostics")
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
    "KBD_HISTORY_DEPTH",
    "keyboard_complete_line",
    "keyboard_add_history",
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
grep -q -- 'fj_usbhost_pio_wait_tx_stall' "$kernel_usbhost_pre_stall_patch"
grep -q -- 'FJ_PIO_FDEBUG_TXSTALL' "$kernel_usbhost_pre_stall_patch"
grep -q -- 'Pico-PIO-USB' "$kernel_usbhost_pre_stall_patch"
grep -q -- 'post-PRE stall/idle point' "$kernel_usbhost_pre_stall_patch"
echo "ok usbhost PRE TX idle guard"
grep -q -- 'hcd_manual_start' "$kernel_usbhost_manual_hcd_patch"
grep -q -- 'hcd-start' "$kernel_usbhost_manual_hcd_patch"
grep -q -- 'raspberrypi,hcd-manual-start' "$kernel_usbhost_manual_hcd_patch"
grep -q -- 'USB host HCD waiting for hcd-start' "$kernel_usbhost_manual_hcd_patch"
grep -q -- 'raspberrypi,hcd-start-delay-ms = <8000>' "$dts_src"
grep -q -- 'raspberrypi,hcd-port-reset-settle-ms = <500>' "$dts_src"
grep -q -- 'raspberrypi,hcd-port-reset-sof-frames = <25>' "$dts_src"
grep -q -- 'raspberrypi,hcd-manual-start' "$dts_src"
	grep -q -- 'hcd-start' "$usbhost_src"
	grep -q -- 'hcd_manual_start' "$usbhost_src"
	grep -q -- 'HCD_START_POWER_OFF_US' "$usbhost_src"
	grep -q -- 'HCD_START_POWER_ON_US' "$usbhost_src"
	grep -q -- 'HCD_START_POST_RESET_US' "$usbhost_src"
	grep -q -- 'bridge_hcd_start' "$usbhost_src"
	grep -q -- 'bridge_write_command("reset 100")' "$usbhost_src"
	grep -q -- 'manual-start %d' "$usbhost_src"
	grep -q -- 'FJ_USBHOST_HCD_PORT_RESET_SETTLE_MS 250u' "$kernel_usbhost_reset_settle_patch"
	grep -q -- 'FJ_USBHOST_HCD_PORT_RESET_SETTLE_MS_DEFAULT 500u' "$kernel_usbhost_reset_settle_config_patch"
	grep -q -- 'hcd_port_reset_settle_ms' "$kernel_usbhost_reset_settle_config_patch"
	grep -q -- 'raspberrypi,hcd-port-reset-settle-ms' "$kernel_usbhost_reset_settle_config_patch"
	grep -q -- 'uh->hcd_port_reset_settle_ms' "$kernel_usbhost_reset_settle_status_patch"
	grep -q -- 'reset-settle-ms %d' "$usbhost_src"
	grep -q -- 'fj_usbhost_hcd_port_reset_settle' "$kernel_usbhost_reset_settle_patch"
	grep -q -- 'hcd-port-reset-settle-ms=%u' "$kernel_usbhost_reset_settle_patch"
	grep -q -- 'FJ_USBHOST_HCD_PORT_RESET_SOF_FRAMES_DEFAULT 25u' "$kernel_usbhost_reset_sof_patch"
	grep -q -- 'hcd_port_reset_sof_frames' "$kernel_usbhost_reset_sof_patch"
	grep -q -- 'raspberrypi,hcd-port-reset-sof-frames' "$kernel_usbhost_reset_sof_patch"
	grep -q -- 'sof-frames=%u' "$kernel_usbhost_reset_sof_patch"
	grep -q -- 'data-ack-tail-drain-us %d' "$usbhost_src"
	grep -q -- '"hcd_data_ack_tail_drain_us"' "$usbhost_src"
	grep -q -- '"hcd_data_ack_tail_drain_us"' "$airlift_src"
	grep -q -- 'mutex_trylock(&uh->lock)' "$kernel_usbhost_status_busy_patch"
	grep -q -- 'status-busy lock-held' "$kernel_usbhost_status_busy_patch"
	grep -q -- 'pio-packet-io-busy' "$kernel_usbhost_status_busy_patch"
	grep -q -- 'FJ_USBHOST_HCD_PRESTART_POWER_OFF_MS 250u' "$kernel_usbhost_prestart_power_patch"
	grep -q -- 'FJ_USBHOST_HCD_PRESTART_POWER_ON_MS 750u' "$kernel_usbhost_prestart_power_patch"
	grep -q -- 'FJ_USBHOST_HCD_PRESTART_RESET_MS 100u' "$kernel_usbhost_prestart_power_patch"
	grep -q -- 'fj_usbhost_hcd_prestart_power_cycle' "$kernel_usbhost_prestart_power_patch"
	grep -q -- 'hcd-prestart-power-cycle off-ms=%u on-ms=%u reset-ms=%u' "$kernel_usbhost_prestart_power_patch"
	grep -q -- 'USB host HCD pre-start power cycle complete' "$kernel_usbhost_prestart_power_patch"
	grep -q -- 'FJ_USBHOST_HCD_FAULT_RECOVER_DELAY_MS 1000u' "$kernel_usbhost_auto_recover_patch"
	grep -q -- 'hcd_fault_recover_work' "$kernel_usbhost_auto_recover_patch"
	grep -q -- 'scheduling automatic recovery' "$kernel_usbhost_auto_recover_patch"
	grep -q -- 'hcd-auto-recover %u start' "$kernel_usbhost_auto_recover_patch"
	grep -q -- 'hcd-auto-recover %u done' "$kernel_usbhost_auto_recover_patch"
	grep -q -- 'usb_hcd_poll_rh_status' "$kernel_usbhost_auto_recover_patch"
	grep -q -- 'FJ_USBHOST_HCD_FAULT_RECOVER_MAX 2u' "$kernel_usbhost_auto_recover_cap_patch"
	grep -q -- 'auto recovery limit reached' "$kernel_usbhost_auto_recover_cap_patch"
	grep -q -- 'hcd-fault auto-recovery-limit=%u' "$kernel_usbhost_auto_recover_cap_patch"
	grep -q -- 'FJ_USBHOST_HCD_DATA_ACK_TAIL_DRAIN_US 4u' "$kernel_usbhost_data_ack_timing_patch"
	grep -q -- "Match Pico-PIO-USB's data receive path" "$kernel_usbhost_data_ack_timing_patch"
	grep -q -- 'the device expects the handshake immediately after EOP' "$kernel_usbhost_data_ack_timing_patch"
	grep -q -- 'hcd_data_ack_tail_drain_us %u' "$kernel_usbhost_data_ack_timing_patch"
		grep -q -- 'use Pico RX lifecycle for Fruit Jam HCD IN' "$kernel_usbhost_hcd_in_pico_patch"
		grep -q -- 'zero-byte timeout on the following IN data stage' "$kernel_usbhost_hcd_in_pico_patch"
		grep -q -- 'fj_usbhost_pio_prepare_receive_pico(uh)' "$kernel_usbhost_hcd_in_pico_patch"
		grep -q -- 'mask IRQs for Fruit Jam HCD IN window' "$kernel_usbhost_hcd_in_irq_patch"
		grep -q -- 'fj_usbhost_pio_hcd_in_token_receive' "$kernel_usbhost_hcd_in_irq_patch"
		grep -q -- 'local_irq_save(flags)' "$kernel_usbhost_hcd_in_irq_patch"
		grep -q -- "does not miss the hub's EP0 DATA turnaround window" "$kernel_usbhost_hcd_in_irq_patch"
		grep -q -- 'bound Fruit Jam HCD IN IRQ window' "$kernel_usbhost_hcd_in_irq_bound_patch"
		grep -q -- 'FJ_USBHOST_HCD_IN_IRQ_RX_TIMEOUT_US 120u' "$kernel_usbhost_hcd_in_irq_bound_patch"
		grep -q -- 'fj_usbhost_pio_receive_raw_timeout' "$kernel_usbhost_hcd_in_irq_bound_patch"
		grep -q -- 'should only cover the immediate USB response/handshake interval' "$kernel_usbhost_hcd_in_irq_bound_patch"
		grep -q -- 'prev_summary' "$kernel_usbhost_fault_summary_patch"
		grep -q -- 'hcd-fault ep0-failures=%u last-ret=%d pid=0x%02x rx-len=%u prev=%s' "$kernel_usbhost_fault_summary_patch"
		grep -q -- 'USB host HCD faulted after %u EP0 failures ret=%d pid=0x%02x rx-len=%u prev=%s' "$kernel_usbhost_fault_summary_patch"
	grep -q -- 'fj_usbhost_pio_prepare_receive_pico' "$kernel_usbhost_pico_rx_lifecycle_patch"
	grep -q -- 'fj_usbhost_pio_finish_receive_pico' "$kernel_usbhost_pico_rx_lifecycle_patch"
	grep -q -- 'The EOP detector keeps running' "$kernel_usbhost_pico_rx_lifecycle_patch"
	grep -q -- 'fj_usbhost_pio_prepare_receive_pico(uh)' "$kernel_usbhost_pico_rx_lifecycle_patch"
	grep -q -- 'fj_usbhost_pio_finish_receive_pico(uh, ret, len)' "$kernel_usbhost_pico_rx_lifecycle_patch"
	grep -q -- 'match Pico-PIO-USB handshake wait bounds' "$kernel_usbhost_pico_wait_bounds_patch"
	grep -q -- 'waited <= start_timeout' "$kernel_usbhost_pico_wait_bounds_patch"
	grep -q -- 'waited > start_timeout' "$kernel_usbhost_pico_wait_bounds_patch"
	grep -q -- 'waited <= FJ_USBHOST_HCD_HANDSHAKE_PACKET_US' "$kernel_usbhost_pico_wait_bounds_patch"
	grep -q -- 'keep Fruit Jam HCD EOP alive after RX' "$kernel_usbhost_eop_alive_patch"
	grep -q -- 'Pico-PIO-USB/wili8jam transaction tail' "$kernel_usbhost_eop_alive_patch"
	grep -q -- 'leave the EOP detector running between EP0 phases' "$kernel_usbhost_eop_alive_patch"
	grep -q -- 'fj_usbhost_pio_sm_clear_fifos(uh, uh->sm_rx)' "$kernel_usbhost_eop_alive_patch"
	echo "ok usbhost automatic delayed HCD start guard"
	echo "ok usbhost HCD reset settle guard"
	echo "ok usbhost HCD fault summary guard"
	echo "ok usbhost Pico RX lifecycle guard"
	echo "ok usbhost Pico handshake wait guard"
	echo "ok usbhost EOP alive RX finish guard"
	python3 -m py_compile "$wili8jam_usb_compare_src"
	if [ -f "$wili8jam_root/usb-host/tusb_config.h" ]; then
		wili8jam_usb_compare_out=$(python3 "$wili8jam_usb_compare_src" --wili8jam-root "$wili8jam_root")
		printf '%s\n' "$wili8jam_usb_compare_out" | grep -q 'wili8jam USB config compare: ok'
	else
		echo "skip wili8jam USB config compare: missing $wili8jam_root"
	fi
	grep -q -- 'CFG_TUH_XINPUT' "$wili8jam_usb_compare_src"
	grep -q -- 'CONFIG_JOYSTICK_XPAD' "$wili8jam_usb_compare_src"
	grep -q -- 'raspberrypi,tx-dma-channel = <9>' "$wili8jam_usb_compare_src"
	grep -q -- 'logitech receiver usb' "$wili8jam_usb_compare_src"
	echo "ok wili8jam USB reference compare guard"
	python3 -m py_compile "$wili8jam_media_compare_src"
	if [ -f "$wili8jam_root/src/dvi.c" ]; then
		wili8jam_media_compare_out=$(python3 "$wili8jam_media_compare_src" --wili8jam-root "$wili8jam_root")
		printf '%s\n' "$wili8jam_media_compare_out" | grep -q 'wili8jam media config compare: ok'
	else
		echo "skip wili8jam media config compare: missing $wili8jam_root"
	fi
	grep -q -- 'DVI via HSTX (640x480@60Hz)' "$wili8jam_media_compare_src"
	grep -q -- 'Audio: I2S + DAC ready' "$wili8jam_media_compare_src"
	grep -q -- 'adafruit,i2s-gpios = <24 25 26 27 23>' "$wili8jam_media_compare_src"
	grep -q -- 'raspberrypi,mclk-hz = <15000000>' "$wili8jam_media_compare_src"
	grep -q -- 'Fruit Jam HSTX DVI registered at %ux%u RGB332' "$wili8jam_media_compare_src"
	grep -q -- 'FJ_DVI_WILI_FB_WIDTH' "$wili8jam_media_compare_src"
	grep -q -- 'DMA_CTRL_SIZE_HALFWORD' "$wili8jam_media_compare_src"
	grep -q -- 'wili-pattern' "$wili8jam_media_compare_src"
	grep -q -- 'add wili8jam RGB565 DVI mode' "$kernel_dvi_wili_patch"
	grep -q -- 'FJ_DVI_WILI_FB_WIDTH' "$kernel_dvi_wili_patch"
	grep -q -- 'HSTX_EXPAND_TMDS_L2_NBITS(4)' "$kernel_dvi_wili_patch"
	grep -q -- 'HSTX_EXPAND_SHIFT_ENC_N(FJ_DVI_WILI_SCALE)' "$kernel_dvi_wili_patch"
	grep -q -- 'DMA_CTRL_SIZE_HALFWORD' "$kernel_dvi_wili_patch"
	grep -q -- 'wili-test' "$kernel_dvi_wili_patch"
	echo "ok wili8jam media reference compare guard"
	grep -q -- 'add wili8jam audio waveforms' "$kernel_audio_waveform_patch"
	grep -q -- 'tone HZ MS WAVEFORM' "$kernel_audio_waveform_patch"
	grep -q -- 'wave WAVEFORM HZ MS' "$kernel_audio_waveform_patch"
	grep -q -- 'FJ_AUDIO_WAVE_SQUARE' "$kernel_audio_waveform_patch"
	grep -q -- 'FJ_AUDIO_WAVE_NOISE' "$kernel_audio_waveform_patch"
	grep -q -- 'phase - phase_step' "$kernel_audio_waveform_patch"
	grep -q -- 'fj_audio_wave_sample' "$kernel_audio_waveform_patch"
	grep -q -- 'last_waveform' "$kernel_audio_waveform_patch"
	grep -q -- '--waveform WAVE' "$rtttl_src_c"
	grep -q -- 'I2S_WAVE_NOISE' "$rtttl_src_c"
	grep -q -- 'tone %u %u %s' "$rtttl_src_c"
	grep -q -- 'audio_waveform_args' "$berry_src/fruitjam.be"
	grep -q -- 'fruitjam.audio_tone_command = def(hz, ms, loud, backend, waveform)' "$berry_src/fruitjam.be"
	grep -q -- 'fruitjam.rtttl_command = def(song, loud, backend, waveform)' "$berry_src/fruitjam.be"
	echo "ok wili8jam audio waveform guard"

	echo "== console source guards =="
python3 -m py_compile "$cdc_smoke_src"
grep -q -- '--usb-keyboard' "$cdc_smoke_src"
grep -q -- 'usbhost_keyboard_tests' "$cdc_smoke_src"
echo "ok cdc smoke usb keyboard guard"
python3 -m py_compile "$usb_keyboard_smoke_src"
usb_keyboard_smoke_out=$("$usb_keyboard_smoke_src" --self-test --require-input)
printf '%s\n' "$usb_keyboard_smoke_out" | grep -q 'board shell preflight'
printf '%s\n' "$usb_keyboard_smoke_out" | grep -q '7 passed, 0 failed'
grep -q -- '--transport' "$usb_keyboard_smoke_src"
grep -q -- '--shell-probe-timeout' "$usb_keyboard_smoke_src"
grep -q -- '--serial-open-timeout' "$usb_keyboard_smoke_src"
grep -q -- 'def _probe_serial_open' "$usb_keyboard_smoke_src"
grep -q -- 'subprocess.run' "$usb_keyboard_smoke_src"
grep -q -- 'kbd-auto-shell' "$usb_keyboard_smoke_src"
echo "ok focused usb keyboard smoke guard"
python3 -m py_compile "$usbhost_hcd_smoke_src"
usbhost_hcd_smoke_out=$("$usbhost_hcd_smoke_src" --self-test)
printf '%s\n' "$usbhost_hcd_smoke_out" | grep -q 'logitech receiver usb'
printf '%s\n' "$usbhost_hcd_smoke_out" | grep -q 'hid keyboard input'
printf '%s\n' "$usbhost_hcd_smoke_out" | grep -q 'external hub usb'
printf '%s\n' "$usbhost_hcd_smoke_out" | grep -q 'xbox receiver usb'
printf '%s\n' "$usbhost_hcd_smoke_out" | grep -q 'xpad gamepad input'
printf '%s\n' "$usbhost_hcd_smoke_out" | grep -q 'hcd not faulted'
printf '%s\n' "$usbhost_hcd_smoke_out" | grep -q 'wili8jam electrical config'
printf '%s\n' "$usbhost_hcd_smoke_out" | grep -q 'hcd service window'
printf '%s\n' "$usbhost_hcd_smoke_out" | grep -q 'usbhost bridge pre-start'
printf '%s\n' "$usbhost_hcd_smoke_out" | grep -q 'usbhost hcd start'
printf '%s\n' "$usbhost_hcd_smoke_out" | grep -q 'usbhost hcd status'
printf '%s\n' "$usbhost_hcd_smoke_out" | grep -q '22 passed, 0 failed'
printf '%s\n' "$usbhost_hcd_smoke_out" | grep -q '0 failed'
grep -q -- '--self-test' "$usbhost_hcd_smoke_src"
grep -q -- 'USB_DEVICE_REGISTRY_CMD' "$usbhost_hcd_smoke_src"
grep -q -- 'KERNEL_RELEASE_CMD' "$usbhost_hcd_smoke_src"
grep -q -- 'HCD_START_CMD' "$usbhost_hcd_smoke_src"
grep -q -- 'HCD_STATUS_CMD' "$usbhost_hcd_smoke_src"
grep -q -- 'ROOT_HUB_MAXCHILD = "1"' "$usbhost_hcd_smoke_src"
grep -q -- 'def root_hub_port_count_ok' "$usbhost_hcd_smoke_src"
grep -q -- 'def external_hub_present' "$usbhost_hcd_smoke_src"
grep -q -- 'maxchild=4' "$usbhost_hcd_smoke_src"
grep -q -- 'CH334F USB2.0 Hub' "$usbhost_hcd_smoke_src"
grep -q -- 'fruitjam-usbhost hcd-start' "$usbhost_hcd_smoke_src"
grep -q -- 'fruitjam-usbhost status' "$usbhost_hcd_smoke_src"
grep -q -- 'manual-start 1' "$usbhost_hcd_smoke_src"
grep -q -- 'reset-settle-ms 500' "$usbhost_hcd_smoke_src"
grep -q -- 'data-ack-tail-drain-us 4' "$usbhost_hcd_smoke_src"
grep -q -- 'def _probe_serial_open' "$usbhost_hcd_smoke_src"
grep -q -- 'xpad_input_present' "$usbhost_hcd_smoke_src"
grep -q -- 'hcd_not_faulted' "$usbhost_hcd_smoke_src"
grep -q -- 'wili8jam_config_present' "$usbhost_hcd_smoke_src"
grep -q -- 'self._api("status", timeout)' "$usbhost_hcd_smoke_src"
grep -q -- 'self._api("hcd-start", timeout)' "$usbhost_hcd_smoke_src"
grep -q -- 'time.sleep(2)' "$usbhost_hcd_smoke_src"
grep -q -- '"HTTP API error:"' "$usbhost_hcd_smoke_src"
grep -q -- '"telnet error:"' "$usbhost_hcd_smoke_src"
grep -q -- 'def check_output_if_ok' "$usbhost_hcd_smoke_src"
grep -q -- 'def auto_transports' "$usbhost_hcd_smoke_src"
grep -q -- 'preflight_failures.append' "$usbhost_hcd_smoke_src"
echo "ok usbhost hcd smoke guard"
python3 -m py_compile "$recover_flash_src"
grep -q -- '--http-host' "$recover_flash_src"
grep -q -- 'FJ_HTTP_HOST' "$recover_flash_src"
grep -q -- 'action=bootsel' "$recover_flash_src"
grep -q -- '--post-trigger-bootsel-timeout' "$recover_flash_src"
grep -q -- 'FJ_POST_TRIGGER_BOOTSEL_TIMEOUT' "$recover_flash_src"
grep -q -- '--manual-bootsel-timeout' "$recover_flash_src"
grep -q -- 'FJ_MANUAL_BOOTSEL_TIMEOUT' "$recover_flash_src"
grep -q -- 'FJ_BARK_URL' "$recover_flash_src"
grep -q -- '--bark-url' "$recover_flash_src"
grep -q -- 'FJ_AIRLIFT_DISCOVERY' "$recover_flash_src"
grep -q -- 'FJ_UART_PORT' "$recover_flash_src"
grep -q -- 'FJ_UART_BAUD' "$recover_flash_src"
grep -q -- 'FJ_UART_DISCOVERY' "$recover_flash_src"
grep -q -- 'def auto_airlift_hosts' "$recover_flash_src"
grep -q -- 'def auto_uart_ports' "$recover_flash_src"
grep -q -- 'esp32c6-' "$recover_flash_src"
grep -q -- '--skip-airlift-discovery' "$recover_flash_src"
grep -q -- '--uart-port' "$recover_flash_src"
grep -q -- '--skip-uart' "$recover_flash_src"
grep -q -- '--skip-uart-discovery' "$recover_flash_src"
grep -q -- 'def recovery_hosts' "$recover_flash_src"
grep -q -- 'reboot bootsel' "$recover_flash_src"
grep -q -- 'b"bootsel 250\\n"' "$recover_flash_src"
grep -q -- 'request_sent = False' "$recover_flash_src"
grep -q -- 'socket closed before reply' "$recover_flash_src"
		grep -q -- 'def cdc_counterpart' "$recover_flash_src"
		grep -q -- '--include-tty-counterpart' "$recover_flash_src"
		grep -q -- '--no-tty-counterpart' "$recover_flash_src"
		grep -q -- 'FJ_INCLUDE_TTY_COUNTERPART' "$recover_flash_src"
			grep -q -- 'os.environ.get("FJ_INCLUDE_TTY_COUNTERPART", "1")' "$recover_flash_src"
		grep -q -- 'include_tty_counterparts or sys.platform != "darwin"' "$recover_flash_src"
		grep -q -- '"/dev/tty."' "$recover_flash_src"
	grep -q -- 'def close_own_fds_for_path' "$recover_flash_src"
	grep -q -- 'closed lingering CDC fd' "$recover_flash_src"
	grep -q -- 'close_own_fds_for_path(port, verbose)' "$recover_flash_src"
	grep -q -- 'def run_serial_child' "$recover_flash_src"
	grep -q -- 'sys.executable, "-c", code' "$recover_flash_src"
	grep -q -- 'def uart_shell_bootsel' "$recover_flash_src"
	grep -q -- 'ser.dtr = False' "$recover_flash_src"
	grep -q -- 'ser.rts = False' "$recover_flash_src"
	grep -q -- 'sent BOOTSEL commands over UART' "$recover_flash_src"
	grep -q -- 'sent BOOTSEL commands over CDC' "$recover_flash_src"
		grep -q -- 'def cdc_raw_shell_bootsel' "$recover_flash_src"
		grep -q -- 'os.O_NONBLOCK' "$recover_flash_src"
		grep -q -- 'CDC raw shell BOOTSEL on' "$recover_flash_src"
		grep -q -- 'cdc_raw_shell_bootsel(port, args.serial_open_timeout, args.verbose)' "$recover_flash_src"
		grep -q -- 'sent raw CDC BOOTSEL commands over' "$recover_flash_src"
		grep -q -- '--skip-cdc-raw-shell' "$recover_flash_src"
grep -q -- 'def telnet_immediate_bootsel' "$recover_flash_src"
grep -q -- 'def notify_bark' "$recover_flash_src"
grep -q -- 'Fruit Jam BOOTSEL needed' "$recover_flash_src"
grep -q -- 'BOOTSEL appeared during fallback wait' "$recover_flash_src"
grep -q -- '--watch-only' "$recover_flash_src"
grep -q -- 'def watch_only_bootsel' "$recover_flash_src"
grep -q -- 'no recovery triggers will be sent' "$recover_flash_src"
grep -q -- 'def picotool_force_bootsel' "$recover_flash_src"
grep -q -- '"reboot", "-u", "-f"' "$recover_flash_src"
grep -q -- '--skip-picotool-force' "$recover_flash_src"
grep -q -- 'picotool_force_bootsel(args.picotool, args.verbose)' "$recover_flash_src"
	grep -q -- 'sent immediate BOOTSEL command over telnet' "$recover_flash_src"
	grep -q -- 'bootsel 250' "$recover_flash_src"
	grep -q -- 'def cdc_1200_stty_bootsel' "$recover_flash_src"
	grep -q -- 'def cdc_1200_native_touch_bootsel' "$recover_flash_src"
	grep -q -- 'termios.TIOCMSET' "$recover_flash_src"
	grep -q -- '"1200", "hupcl"' "$recover_flash_src"
	grep -q -- 'sent native 1200-baud DTR-low BOOTSEL touch on CDC' "$recover_flash_src"
	grep -q -- 'sent stty 1200-baud BOOTSEL touch on CDC' "$recover_flash_src"
grep -q -- '"load", "-fu"' "$recover_flash_src"
grep -q -- 'DEFAULT_FLASH_TIMEOUT = float(os.environ.get("FJ_FLASH_TIMEOUT", "180"))' "$recover_flash_src"
grep -q -- 'LINUX_REBOOT_CMD_RESTART2' "$fruitjam_shell_src"
grep -q -- 'reboot_bootsel' "$fruitjam_shell_src"
grep -q -- 'builtins: bootsel cd echo exit help history status' "$fruitjam_shell_src"
grep -q -- 'maybe_direct_bootsel' "$telnetd_src"
grep -q -- 'MSG_PEEK' "$telnetd_src"
grep -q -- 'fruitjam-telnetd: reboot bootsel' "$telnetd_src"
grep -q -- 'ACM_RP2350_BOOTSEL_BAUD' "$kernel_cdc_bootsel_patch"
grep -q -- 'kernel_restart("bootsel")' "$kernel_cdc_bootsel_patch"
grep -q -- 'mod_delayed_work' "$kernel_cdc_bootsel_patch"
grep -q -- 'RP2350_PSM_WDSEL_ROSC' "$kernel_bootsel_restart_patch"
grep -q -- 'RP2350_PSM_WDSEL_XOSC' "$kernel_bootsel_restart_patch"
grep -q -- 'RP2350_PSM_WDSEL_BITS &' "$kernel_bootsel_restart_patch"
grep -q -- 'RP2350_BOOTROM_BOOT_TYPE_BOOTSEL' "$kernel_bootsel_restart_patch"
grep -q -- 'RP2350_BOOTLOADER_RESCUE_MAGIC' "$kernel_bootsel_restart_patch"
grep -q -- 'early_initcall(rp2350_disarm_rescue_watchdog)' "$kernel_bootsel_restart_patch"
grep -q -- 'writel(0, watchdog + RP2350_WATCHDOG_CTRL)' "$kernel_bootsel_restart_patch"
grep -q -- 'BR2_PACKAGE_PICO2_BOOTLOADER_RESCUE_WATCHDOG_MS=15000' "$defconfig_src"
	grep -q -- 'WATCHDOG_BOOTSEL_VECTOR_MAGIC' "$bootloader_main_src"
	grep -q -- 'watchdog_bootsel_vector_fired' "$bootloader_main_src"
	grep -q -- 'BOOTSEL watchdog vector reached bootloader' "$bootloader_main_src"
	grep -F -q -- 'def parse_fdt' "$validate_image_src"
	grep -F -q -- 'DTB has wrong magic' "$validate_image_src"
	grep -F -q -- 'adafruit_fruit_jam_rp2350.dtb' "$validate_image_src"
		grep -F -q -- 'require_u32s(dt_props, "/clk-sys", "clock-frequency", [252000000])' "$validate_image_src"
		grep -F -q -- 'require_u32s(dt_props, "/fruitjam-pins", "adafruit,usb-host-gpios", [1, 2, 11])' "$validate_image_src"
		grep -F -q -- 'require_u32s(dt_props, "/fruitjam-pins", "adafruit,i2c-gpios", [20, 21])' "$validate_image_src"
		grep -F -q -- 'require_u32s(dt_props, "/fruitjam-pins", "adafruit,i2s-gpios", [24, 25, 26, 27, 23])' "$validate_image_src"
		grep -F -q -- 'require_u32s(dt_props, "/fruitjam-pins", "adafruit,dvi-gpios", [12, 13, 14, 15, 16, 17, 18, 19])' "$validate_image_src"
		grep -F -q -- 'usbhost_path = "/soc/usbhost-bridge@d0000000"' "$validate_image_src"
		grep -F -q -- '("raspberrypi,tx-dma-channel", [9])' "$validate_image_src"
		grep -F -q -- '("raspberrypi,hcd-start-delay-ms", [8000])' "$validate_image_src"
		grep -F -q -- '("raspberrypi,hcd-port-reset-settle-ms", [500])' "$validate_image_src"
		grep -F -q -- '("raspberrypi,hcd-port-reset-sof-frames", [25])' "$validate_image_src"
		grep -F -q -- 'DTB usbhost bridge must set raspberrypi,hcd-manual-start' "$validate_image_src"
		grep -F -q -- 'b"bridge HCD start power-on failed"' "$validate_image_src"
		grep -F -q -- 'b"bridge HCD start bus-reset failed"' "$validate_image_src"
		grep -F -q -- 'audio_path = "/soc/audio-clock@50300000"' "$validate_image_src"
		grep -F -q -- '("raspberrypi,mclk-hz", [15000000])' "$validate_image_src"
		grep -F -q -- 'dvi_path = "/soc/dvi@400c0000"' "$validate_image_src"
		grep -F -q -- 'b"Fruit Jam audio clock ready on /dev/fruitjam-audio"' "$validate_image_src"
		grep -F -q -- 'b"Fruit Jam HSTX DVI registered at %ux%u RGB332, idle"' "$validate_image_src"
			grep -F -q -- 'b"hcd-start"' "$validate_image_src"
			grep -F -q -- 'b"hcd_port_reset_settle_ms %u"' "$validate_image_src"
			grep -F -q -- 'b"hcd_port_reset_sof_frames %u"' "$validate_image_src"
			grep -F -q -- 'b"hcd_data_ack_tail_drain_us %u"' "$validate_image_src"
			grep -F -q -- 'b"hcd-port-reset-settle-ms=%u"' "$validate_image_src"
			grep -F -q -- 'b"sof-frames=%u"' "$validate_image_src"
			grep -F -q -- 'b"status-busy lock-held"' "$validate_image_src"
			grep -F -q -- 'b"hcd-prestart-power-cycle off-ms=%u on-ms=%u reset-ms=%u"' "$validate_image_src"
			grep -F -q -- 'b"USB host HCD pre-start power cycle complete"' "$validate_image_src"
			grep -F -q -- 'b"scheduling automatic recovery"' "$validate_image_src"
			grep -F -q -- 'b"hcd-auto-recover %u done"' "$validate_image_src"
			grep -F -q -- 'b"auto recovery limit reached"' "$validate_image_src"
			grep -F -q -- 'b"hcd-fault auto-recovery-limit=%u"' "$validate_image_src"
			grep -F -q -- 'b"hcd-fault ep0-failures=%u last-ret=%d pid=0x%02x rx-len=%u prev=%s"' "$validate_image_src"
			grep -F -q -- 'b"usb-devices"' "$validate_image_src"
		grep -F -q -- 'b"dev-input"' "$validate_image_src"
		grep -F -q -- 'b"input-registry"' "$validate_image_src"
		grep -F -q -- 'b"Xbox 360 Wireless Receiver (XBOX)"' "$validate_image_src"
		echo "ok image USB/media artifact guard"
	python3 - "$recover_flash_src" <<'PY'
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text()
if "return False\n                sock.sendall" in source:
    raise SystemExit("prompt-aware telnet BOOTSEL send is unreachable")
needle = 'sock.sendall(b"\\r\\nbootsel 250\\r\\n/usr/bin/fruitjamctl bootsel 250\\r\\n")'
if needle not in source:
    raise SystemExit("prompt-aware telnet BOOTSEL payload is missing")
request = source.split("def request_bootsel", 1)[1]
immediate = request.find("telnet_immediate_bootsel")
prompt = request.find("telnet_bootsel")
if immediate < 0 or prompt < 0 or immediate > prompt:
    raise SystemExit("telnet recovery must send immediate BOOTSEL before prompt-aware shell fallback")
port_loop = request.split("for port in ports:", 1)[1].split("return picotool_info", 1)[0]
uart_loop = request.split("for port in uart_ports:", 1)[1].split("ports = selected_cdc_ports", 1)[0]
if "uart_shell_bootsel(port, args.uart_baud, args.serial_open_timeout, args.verbose)" not in uart_loop:
    raise SystemExit("UART recovery loop is missing the DTR/RTS-disabled shell BOOTSEL method")
native_touch = port_loop.find("cdc_1200_native_touch_bootsel")
shell_touch = port_loop.find("cdc_shell_bootsel")
raw_touch = port_loop.find("cdc_raw_shell_bootsel")
stty_touch = port_loop.find("cdc_1200_stty_bootsel")
if min(native_touch, shell_touch, raw_touch, stty_touch) < 0:
    raise SystemExit("CDC recovery loop is missing one of the expected recovery methods")
if not (native_touch < shell_touch < raw_touch < stty_touch):
    raise SystemExit("CDC native 1200-baud touch must run before shell-open fallbacks")
uart_ports = request.find("uart_ports = selected_uart_ports")
cdc_ports = request.find("ports = selected_cdc_ports")
if uart_ports < 0 or cdc_ports < 0 or uart_ports > cdc_ports:
    raise SystemExit("UART recovery should run before USB CDC fallbacks")
main = source.split("def main", 1)[1]
watch = main.find("watch_only_bootsel(args)")
request_call = main.find("request_bootsel(args)")
if watch < 0 or request_call < 0 or watch > request_call:
    raise SystemExit("watch-only mode must bypass automatic recovery triggers")
if "if not args.watch_only:\n                notify_bark" not in source:
    raise SystemExit("manual BOOTSEL notification must stay on automatic recovery failure only")
if "Bark notification skipped: no FJ_BARK_URL/BARK_URL configured" not in source:
    raise SystemExit("manual BOOTSEL notification path must report missing Bark config in verbose mode")
PY
if grep -q -- '^+.*USB_CDC_CTRL_DTR' "$kernel_cdc_bootsel_patch"; then
	echo "CDC 1200-baud recovery must not depend on a separate DTR transition" >&2
	exit 1
fi
if grep -q -- 'RP2350_PSM_WDSEL_PROC_COLD' "$kernel_bootsel_restart_patch"; then
	echo "BOOTSEL watchdog reset must not exclude PROC_COLD; match Pico SDK ROSC/XOSC exclusion" >&2
	exit 1
fi
python3 - "$kernel_bootsel_restart_patch" <<'PY'
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text()
needle = (
    "RP2350_PSM_WDSEL_BITS &\n"
    "+\t       ~(RP2350_PSM_WDSEL_ROSC | RP2350_PSM_WDSEL_XOSC)"
)
if needle not in source:
    raise SystemExit("BOOTSEL watchdog reset does not match Pico SDK WDSEL mask")
print("ok bootsel watchdog reset guard")
PY
echo "ok recovery flash helper guard"
python3 -m py_compile "$mqtt_smoke_src"
mqtt_smoke_out=$("$mqtt_smoke_src" --self-test --host broker.local --username user --password validate-secret)
printf '%s\n' "$mqtt_smoke_out" | grep -q 'target mqtt publish'
printf '%s\n' "$mqtt_smoke_out" | grep -q 'target mqtt subscribe'
if printf '%s\n' "$mqtt_smoke_out" | grep -q 'validate-secret'; then
	echo "mqtt smoke leaked password in output" >&2
	exit 1
fi
grep -q -- 'mosquitto_pub --airlift' "$mqtt_smoke_src"
grep -q -- 'mosquitto_sub --airlift' "$mqtt_smoke_src"
echo "ok focused mqtt smoke guard"
grep -q 'ttyAMA0::respawn:/usr/bin/fruitjam-uart-login' "$inittab_src"
if grep -q 'ttyAMA0::respawn:/usr/bin/hush' "$inittab_src"; then
	echo "ttyAMA0 hush respawn loop was reintroduced" >&2
	exit 1
fi
grep -q '/etc/profile.d/\*.sh' "$profile_src"
grep -q 'clear()' "$cls_src"
grep -q 'cls()' "$cls_src"
python3 - "$web_cgi_src" "$airlift_src" "$httpd_src" <<'PY'
import sys
from pathlib import Path

cgi = Path(sys.argv[1]).read_text()
airlift = Path(sys.argv[2]).read_text()
httpd = Path(sys.argv[3]).read_text()

for source, name in ((cgi, "CGI"), (airlift, "AirLift")):
    if "LINUX_REBOOT_CMD_RESTART2" not in source or 'SYS_reboot' not in source:
        raise SystemExit(f"{name} BOOTSEL path does not use direct restart2")
    if 'fruitjamctl", "bootsel"' in source:
        raise SystemExit(f"{name} BOOTSEL path reintroduced fruitjamctl exec")
    if '\\"verified\\":false' not in source or "picotool info -a" not in source:
        raise SystemExit(f"{name} BOOTSEL response does not state host verification is required")
if "if (!ret && reboot_bootsel" in airlift:
    raise SystemExit("AirLift BOOTSEL reboot must not depend on successful HTTP reply delivery")
for needle, label in [
    ("LINUX_REBOOT_CMD_RESTART2", "direct restart2 constants"),
    ("query_has_bootsel_action", "action parser"),
    ("serve_direct_bootsel", "direct BOOTSEL endpoint"),
    ("reboot_bootsel_after_delay(1200)", "delayed BOOTSEL restart"),
    ("direct-httpd", "direct HTTPD JSON source"),
    ("action=bootsel", "literal BOOTSEL action match"),
    ("picotool info -a", "host verification response"),
]:
    if needle not in httpd:
        raise SystemExit(f"fruitjam-httpd missing {label}")
if 'fruitjamctl", "bootsel"' in httpd:
    raise SystemExit("fruitjam-httpd BOOTSEL path reintroduced fruitjamctl exec")
if "if (!ret && reboot_bootsel_after_delay" in httpd:
    raise SystemExit("fruitjam-httpd BOOTSEL reboot must not depend on successful HTTP reply delivery")
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
for needle, label in [
    ("airlift_heartbeat_stale_sec = 60", "short AirLift stale heartbeat window"),
    ("pid_cmdline_has", "AirLift monitor pid cmdline verification"),
    ("pid_is_airlift_monitor", "AirLift monitor pidfile ownership check"),
    ("paren[2] == 'Z'", "zombie pid rejection"),
    ("removing stale AirLift monitor pid", "stale monitor pid cleanup log"),
]:
    if needle not in services:
        raise SystemExit(f"fruitjam-services missing {label}")
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
    ("http_usbhost_hcd_start", "direct HCD start helper"),
    ('"hcd-start"', "HTTP HCD start command"),
    ('"hcd-clear-fault"', "HTTP HCD fault clear command"),
    ('"usb-devices"', "HTTP USB devices snapshot"),
    ('"dev-input"', "HTTP dev/input snapshot"),
    ('"input-registry"', "HTTP input registry snapshot"),
    ('"hcd_registered"', "HCD registered JSON field"),
    ('"hcd_ep0_failures"', "HCD EP0 failure JSON field"),
    ('"hcd_port_reset_settle_ms"', "HCD port reset settle JSON field"),
    ('"probe_summary"', "probe summary JSON field"),
    ('"clk_sys_hz"', "USB clock JSON field"),
    ('"tx_dma_channel"', "USB DMA channel JSON field"),
    ('http_status_text_int(bridge_status, "tx_dma", 0)', "USB DMA status parser"),
    ('http_status_text_int(bridge_status, "hcd_port_reset_settle_ms", 0)', "USB reset settle status parser"),
]:
    if needle not in airlift:
        raise SystemExit(f"airliftctl USB API missing {label}")
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
for needle, label in [
    ("AIRLIFT_TELNET_BOOTSEL_PEEK_MS", "AirLift telnet BOOTSEL peek window"),
    ("AIRLIFT_ACCEPTED_CLIENT_GRACE_MS", "AirLift accepted telnet grace window"),
    ("tcp_wait_accepted_client_ready", "AirLift accepted telnet socket readiness"),
    ("proceeding without ESTABLISHED", "AirLift accepted telnet state tolerance"),
    ("read_initial_telnet_payload", "AirLift initial telnet payload read"),
    ("avail_failures", "AirLift initial telnet transient avail tolerance"),
    ("maybe_airlift_telnet_bootsel", "AirLift direct telnet BOOTSEL handler"),
    ("AirLift telnet closed before shell attach; checking BOOTSEL payload",
     "AirLift fast-close direct BOOTSEL peek"),
    ("telnet reboot bootsel", "AirLift direct telnet BOOTSEL error log"),
    ("reboot_bootsel_after_delay(250)", "AirLift direct telnet BOOTSEL restart"),
]:
    if needle not in airlift:
        raise SystemExit(f"airliftctl missing {label}")
if "AIRLIFT_SHELL_IDLE_MS 60000L" not in airlift:
    raise SystemExit("airliftctl AirLift telnet idle timeout must stay short")
if "AIRLIFT_SHELL_PREEMPT_IDLE_MS 15000L" not in airlift or "AirLift telnet stale session preempted" not in airlift:
    raise SystemExit("airliftctl missing stale AirLift telnet preemption")
if "AirLift telnet idle timeout" not in airlift:
    raise SystemExit("airliftctl missing silent stale AirLift telnet idle cleanup")
if "AIRLIFT_TELNET_MAX_MS 300000L" not in airlift or "AirLift telnet max session timeout" not in airlift:
    raise SystemExit("airliftctl missing hard AirLift telnet session cap")
if "tcp_drop_client" not in airlift:
    raise SystemExit("airliftctl missing fast stale AirLift telnet socket drop")
if "!telnet_session_active(&telnet) &&" in airlift:
    raise SystemExit("airliftctl inbound recycle must not be blocked by a stale telnet session")
for needle in ("AIRLIFT_INBOUND_RECYCLE_MS 300000L",
               "AirLift inbound periodic recycle",
               "AirLift shell accept failed; recycling",
               "AirLift HTTP accept failed; recycling",
               "AirLift FTP accept failed; recycling",
               "AirLift telnet poll failed; closing session",
               "AirLift HTTP client failed; keeping inbound server alive",
               "AirLift FTP client failed; keeping inbound server alive"):
    if needle not in airlift:
        raise SystemExit(f"airliftctl missing inbound recycle guard {needle!r}")
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
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$uptime_src"
echo "ok c syntax $uptime_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$du_src"
echo "ok c syntax $du_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$ps_src"
echo "ok c syntax $ps_src"
cc -Wall -Wextra -Wno-deprecated-declarations -Os -fsyntax-only "$pgrep_src"
echo "ok c syntax $pgrep_src"
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
