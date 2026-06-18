#!/usr/bin/env python3
"""Compare Fruit Jam Linux USB host settings with the wili8jam reference."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        raise SystemExit(f"missing required file: {path}") from None


def normalize_define(value: str) -> str:
    value = value.split("//", 1)[0].strip()
    while value.startswith("(") and value.endswith(")"):
        inner = value[1:-1].strip()
        if not inner:
            break
        value = inner
    return value


def parse_defines(text: str) -> dict[str, str]:
    defines: dict[str, str] = {}
    for line in text.splitlines():
        match = re.match(r"\s*#\s*define\s+([A-Za-z0-9_]+)\s+(.+?)\s*$", line)
        if match:
            defines[match.group(1)] = normalize_define(match.group(2))
    return defines


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"missing {label}: {needle}")


def require_define(defines: dict[str, str], name: str, expected: str) -> None:
    actual = defines.get(name)
    if actual != expected:
        raise SystemExit(f"wili8jam {name} = {actual!r}, expected {expected!r}")


def require_config(config: str, symbol: str) -> None:
    require(config, f"{symbol}=y", f"Linux kernel config {symbol}")


def default_wili8jam_root() -> Path:
    return Path.cwd().parent / "wili8jam"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--wili8jam-root",
        type=Path,
        default=default_wili8jam_root(),
        help="path to a local freewili/wili8jam checkout",
    )
    args = parser.parse_args(argv)

    wili = args.wili8jam_root.resolve()
    tusb = read_text(wili / "usb-host" / "tusb_config.h")
    main_cpp = read_text(wili / "src" / "main.cpp")
    pio_cfg = read_text(wili / "Pico-PIO-USB" / "src" / "pio_usb_configuration.h")
    xinput_h = read_text(wili / "usb-host" / "tusb_xinput" / "xinput_host.h")
    usb_cmake = read_text(wili / "usb-host" / "CMakeLists.txt")
    root_cmake = read_text(wili / "CMakeLists.txt")

    defines = parse_defines(tusb)
    for name, expected in (
        ("CFG_TUH_ENABLED", "1"),
        ("CFG_TUH_RPI_PIO_USB", "1"),
        ("BOARD_TUH_RHPORT", "1"),
        ("CFG_TUH_MAX_SPEED", "OPT_MODE_FULL_SPEED"),
        ("CFG_TUH_ENUMERATION_BUFSIZE", "512"),
        ("CFG_TUH_HUB", "1"),
        ("CFG_TUH_HID", "4"),
        ("CFG_TUH_XINPUT", "4"),
        ("CFG_TUH_DEVICE_MAX", "4"),
        ("CFG_TUH_HID_EPIN_BUFSIZE", "64"),
        ("CFG_TUH_HID_EPOUT_BUFSIZE", "64"),
    ):
        require_define(defines, name, expected)
    rhport1 = defines.get("CFG_TUSB_RHPORT1_MODE", "")
    if "OPT_MODE_HOST" not in rhport1 or "OPT_MODE_FULL_SPEED" not in rhport1:
        raise SystemExit("wili8jam CFG_TUSB_RHPORT1_MODE must be host full-speed")

    for needle, label in (
        ("set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Pico-PIO-USB)", "Pico-PIO-USB path before init"),
        ("add_subdirectory(usb-host)", "USB host library"),
        ("${FWUSBHOST_TINYUSB_HOST_SOURCES}", "TinyUSB host sources linked into executable"),
    ):
        require(root_cmake, needle, f"wili8jam root CMake {label}")
    for needle, label in (
        ("src/host/hub.c", "hub host source"),
        ("src/class/hid/hid_host.c", "HID host source"),
        ("src/portable/raspberrypi/pio_usb/hcd_pio_usb.c", "PIO USB HCD source"),
        ("fwUSBHostXInput.cpp", "XInput wrapper"),
        ("tusb_xinput/xinput_host.c", "XInput class driver"),
    ):
        require(usb_cmake, needle, f"wili8jam usb-host CMake {label}")

    for needle, label in (
        ("vreg_set_voltage(VREG_VOLTAGE_1_30)", "1.30 V core"),
        ("set_sys_clock_pll(1260000000, 5, 1)", "252 MHz clk_sys"),
        ("gpio_init(11)", "GPIO11 VBUS init"),
        ("gpio_put(11, 1)", "GPIO11 VBUS enable"),
        ("pio_cfg.pin_dp = 1", "PIO USB D+ GPIO1"),
        ("pio_cfg.tx_ch = 9", "PIO USB TX DMA channel 9"),
        ("tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg)", "TinyUSB host port 1 PIO config"),
        ("tuh_task()", "TinyUSB host polling"),
        ("obUSBHost.m_obXInput", "XInput path"),
    ):
        require(main_cpp, needle, f"wili8jam main {label}")

    for needle, label in (
        ("#define PIO_USB_TX_DEFAULT 0", "PIO TX block 0"),
        ("#define PIO_SM_USB_TX_DEFAULT 0", "PIO TX SM0"),
        ("#define PIO_USB_RX_DEFAULT 0", "PIO RX block 0"),
        ("#define PIO_SM_USB_RX_DEFAULT 1", "PIO RX SM1"),
        ("#define PIO_SM_USB_EOP_DEFAULT 2", "PIO EOP SM2"),
        ("#define PIO_USB_DEVICE_CNT 4", "four devices"),
        ("#define PIO_USB_EP_SIZE 64", "64 byte endpoint packets"),
    ):
        require(pio_cfg, needle, f"wili8jam PIO USB config {label}")
    for needle, label in (
        ("#define CFG_TUH_XINPUT_EPIN_BUFSIZE 64", "XInput IN buffer 64"),
        ("#define CFG_TUH_XINPUT_EPOUT_BUFSIZE 64", "XInput OUT buffer 64"),
        ("XBOX360_WIRELESS", "Xbox 360 wireless receiver"),
        ("XBOX360_WIRED", "Xbox 360 wired controller"),
    ):
        require(xinput_h, needle, f"wili8jam XInput {label}")

    config = read_text(REPO / "board/adafruit/adafruit_fruit_jam_rp2350/adafruit_fruit_jam_rp2350.config")
    dts = read_text(REPO / "board/adafruit/adafruit_fruit_jam_rp2350/dts/sifive/adafruit_fruit_jam_rp2350.dts")
    usbhost = read_text(REPO / "package/fruitjam-utils/src/fruitjam-usbhost.c")
    airlift = read_text(REPO / "package/fruitjam-airlift/src/airliftctl.c")
    hcd_smoke = read_text(REPO / "scripts/usbhost-hcd-smoke.py")
    patch_0099 = read_text(REPO / "board/raspberrypi/raspberrypi-pico2/patches/linux/0099-misc-expand-fruitjam-hcd-transfer-types.patch")
    patch_0102 = read_text(REPO / "board/raspberrypi/raspberrypi-pico2/patches/linux/0102-misc-size-fruitjam-usbhost-tx-buffer-for-xinput.patch")
    patch_0103 = read_text(REPO / "board/raspberrypi/raspberrypi-pico2/patches/linux/0103-misc-add-fruitjam-hcd-pre-for-low-speed-hub-devices.patch")
    patch_0113 = read_text(REPO / "board/raspberrypi/raspberrypi-pico2/patches/linux/0113-misc-default-fruitjam-usbhost-to-wili8jam-config.patch")
    patch_0115 = read_text(REPO / "board/raspberrypi/raspberrypi-pico2/patches/linux/0115-misc-add-fruitjam-usbhost-manual-hcd-start.patch")
    patch_0121 = read_text(REPO / "board/raspberrypi/raspberrypi-pico2/patches/linux/0121-misc-keep-fruitjam-hcd-eop-alive-after-rx.patch")

    for symbol in (
        "CONFIG_FRUITJAM_USBHOST_BRIDGE",
        "CONFIG_USB",
        "CONFIG_USB_ANNOUNCE_NEW_DEVICES",
        "CONFIG_HID",
        "CONFIG_HID_GENERIC",
        "CONFIG_USB_HID",
        "CONFIG_INPUT_EVDEV",
        "CONFIG_INPUT_JOYDEV",
        "CONFIG_INPUT_JOYSTICK",
        "CONFIG_JOYSTICK_XPAD",
    ):
        require_config(config, symbol)

    for needle, label in (
        ("clock-frequency = <252000000>", "252 MHz fixed clock"),
        ("adafruit,usb-host-gpios = <1 2 11>", "USB GPIO map"),
        ("raspberrypi,dp-gpio = <1>", "D+ GPIO1"),
        ("raspberrypi,dm-gpio = <2>", "D- GPIO2"),
        ("raspberrypi,power-gpio = <11>", "VBUS GPIO11"),
        ("raspberrypi,pio = <0>", "PIO0"),
        ("raspberrypi,sm-tx = <0>", "TX SM0"),
        ("raspberrypi,sm-rx = <1>", "RX SM1"),
        ("raspberrypi,sm-eop = <2>", "EOP SM2"),
        ("raspberrypi,tx-dma-channel = <9>", "TX DMA9"),
        ("raspberrypi,clk-sys-hz = <252000000>", "USB host clk_sys"),
        ("raspberrypi,hcd-start-delay-ms = <8000>", "HCD service window"),
        ("raspberrypi,hcd-manual-start", "manual HCD recovery gate"),
        ("raspberrypi,hcd-port-reset-settle-ms = <500>", "CH334F EP0 reset settle"),
        ("raspberrypi,hcd-port-reset-sof-frames = <25>", "CH334F EP0 post-reset SOF guard"),
    ):
        require(dts, needle, f"Fruit Jam DTS {label}")

    for needle, label in (
        ("wili8jam's TinyUSB host stack enables hub, HID, CDC, MSC, and XInput support", "class coverage note"),
        ("bulk IN dispatch for future CDC/MSC paths", "bulk IN support"),
        ("usb_pipeint(urb->pipe) || usb_pipebulk(urb->pipe)", "interrupt/bulk dispatch"),
    ):
        require(patch_0099, needle, f"Linux HCD transfer patch {label}")
    for needle, label in (
        ("FJ_USBHOST_TX_ENCODED_MAX\t192u", "XInput-sized encoded TX buffer"),
        ("FJ_USBHOST_RX_BYTES_MAX\t\t72u", "64 byte data plus PIO metadata"),
    ):
        require(patch_0102, needle, f"Linux HCD XInput buffer patch {label}")
    require(patch_0103, "hcd_need_pre", "Linux HCD low-speed hub PRE support")
    require(patch_0113, "FJ_USBHOST_CLK_SYS_HZ_DEFAULT\t252000000u", "Linux HCD wili8jam clk_sys default")
    require(patch_0113, "FJ_USBHOST_TX_DMA_CHANNEL_DEFAULT 9u", "Linux HCD wili8jam DMA default")
    require(patch_0115, "hcd-start", "Linux HCD retry start command")
    require(
        read_text(REPO / "board/raspberrypi/raspberrypi-pico2/patches/linux/0123-misc-make-fruitjam-hcd-reset-settle-configurable.patch"),
        "FJ_USBHOST_HCD_PORT_RESET_SETTLE_MS_DEFAULT 500u",
        "Linux HCD CH334F EP0 reset settle",
    )
    require(
        read_text(REPO / "board/raspberrypi/raspberrypi-pico2/patches/linux/0124-misc-report-fruitjam-hcd-reset-settle-status-value.patch"),
        "uh->hcd_port_reset_settle_ms",
        "Linux HCD CH334F reset settle status value",
    )
    require(usbhost, "reset-settle-ms %d", "fruitjam-usbhost reset settle status")
    require(usbhost, "reset-sof-frames %d", "fruitjam-usbhost reset SOF status")
    require(usbhost, "data-ack-tail-drain-us %d", "fruitjam-usbhost ACK tail status")
    require(airlift, '"hcd_port_reset_settle_ms"', "AirLift USB reset settle JSON field")
    require(airlift, '"hcd_port_reset_sof_frames"', "AirLift USB reset SOF JSON field")
    require(airlift, '"hcd_data_ack_tail_drain_us"', "AirLift USB ACK tail JSON field")
    require(patch_0121, "Pico-PIO-USB/wili8jam transaction tail", "Linux HCD RX/EOP lifecycle alignment")
    require(patch_0121, "leave the EOP detector running between EP0 phases", "Linux HCD EOP stays alive")
    require(usbhost, "hcd-start", "fruitjam-usbhost HCD retry start")
    require(hcd_smoke, "logitech receiver usb", "Logitech receiver smoke check")
    require(hcd_smoke, "xbox receiver usb", "Xbox receiver smoke check")
    require(hcd_smoke, "xpad gamepad input", "xpad input smoke check")

    print(f"wili8jam USB config compare: ok ({wili})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
