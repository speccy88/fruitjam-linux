#!/usr/bin/env python3
"""Smoke-test the Fruit Jam Linux USB HCD path over the board shell or HTTP API."""

from __future__ import annotations

import argparse
import json
import os
import re
import signal
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
from urllib.error import URLError
from urllib.parse import urlencode
from urllib.request import urlopen


CONTROL_NOISE = b"\x10\x01\x01"
DEFAULT_PORT = os.environ.get("FJ_CDC_PORT", "/dev/tty.usbmodem101")
DEFAULT_BAUD = int(os.environ.get("FJ_CDC_BAUD", "115200"))
DEFAULT_TELNET_HOST = os.environ.get("FJ_TELNET_HOST")
DEFAULT_TELNET_PORT = int(os.environ.get("FJ_TELNET_PORT", "23"))
DEFAULT_HTTP_HOST = os.environ.get("FJ_HTTP_HOST", "192.168.1.7")
DEFAULT_HTTP_URL = os.environ.get("FJ_HTTP_URL")
ROOT_HUB_MAXCHILD = "1"
SHELL_PROBE_MARKER = "FJ_HCD_READY"
KERNEL_RELEASE_CMD = "cat /proc/sys/kernel/osrelease"
HCD_START_CMD = "fruitjam-usbhost hcd-start"
HCD_STATUS_CMD = "fruitjam-usbhost status"
USB_DEVICE_REGISTRY_CMD = (
    "fruitjam-usbhost usb-devices"
)
DEV_INPUT_CMD = "fruitjam-usbhost dev-input"
INPUT_REGISTRY_CMD = "fruitjam-usbhost input-registry"


@dataclass
class CommandResult:
    command: str
    output: str
    rc: int | None
    timed_out: bool


@dataclass
class Check:
    name: str
    ok: bool
    detail: str
    output: str = ""


def decode(data: bytes) -> str:
    return data.decode("utf-8", "replace").replace("\x10\x01\x01", "")


def extract_payload(text: str, begin: str, end: str) -> str:
    start = text.rfind(begin)
    if start >= 0:
        text = text[start + len(begin) :]
    finish = text.find(end)
    if finish >= 0:
        text = text[:finish]

    lines: list[str] = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped == "/ #":
            continue
        if stripped.startswith("/ # "):
            stripped = stripped[4:]
        lines.append(stripped)
    return "\n".join(lines).strip()


def extract_fruitjam_shell_payload(text: str, begin: str, end: str) -> tuple[str, int | None]:
    in_payload = False
    raw_lines: list[str] = []

    for line in text.splitlines():
        stripped = line.strip()
        if stripped == begin:
            in_payload = True
            raw_lines.clear()
            continue
        if in_payload and stripped == end:
            break
        if in_payload:
            raw_lines.append(stripped)

    lines: list[str] = []
    for line in raw_lines:
        if not line or line in ("fj$", "/ #"):
            continue
        if line.startswith("fj$ "):
            continue
        if line.startswith("/ # "):
            continue
        lines.append(line)

    rc: int | None = None
    for i in range(len(lines) - 1, -1, -1):
        if re.fullmatch(r"\d{1,3}", lines[i]):
            rc = int(lines[i])
            del lines[i]
            break
    return "\n".join(lines).strip(), rc


class SerialShell:
    def __init__(self, port: str, baud: int, open_timeout: float, verbose: bool = False):
        try:
            import serial
        except ImportError as exc:  # pragma: no cover
            raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc

        self.verbose = verbose
        self.counter = 0
        self._probe_serial_open(port, baud, open_timeout)
        self.serial = self._open_serial(serial, port, baud, open_timeout)

    @staticmethod
    def _probe_serial_open(port: str, baud: int, open_timeout: float) -> None:
        if open_timeout <= 0:
            return
        code = """
import sys
import time
import serial

ser = serial.Serial(
    sys.argv[1],
    int(sys.argv[2]),
    timeout=0.1,
    write_timeout=1,
    dsrdtr=False,
    rtscts=False,
)
ser.dtr = True
ser.rts = False
time.sleep(0.05)
ser.close()
"""
        try:
            result = subprocess.run(
                [sys.executable, "-c", code, port, str(baud)],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=open_timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(f"serial open timed out after {open_timeout:.0f}s on {port}") from exc
        if result.returncode != 0:
            detail = result.stdout.strip() or f"exit {result.returncode}"
            raise RuntimeError(f"serial open failed on {port}: {detail}")

    @staticmethod
    def _open_serial(serial_module, port: str, baud: int, open_timeout: float):
        class SerialOpenTimeout(Exception):
            pass

        def on_alarm(_signum, _frame):
            raise SerialOpenTimeout

        old_handler = None
        old_timer: tuple[float, float] | None = None
        if hasattr(signal, "SIGALRM") and open_timeout > 0:
            old_handler = signal.getsignal(signal.SIGALRM)
            signal.signal(signal.SIGALRM, on_alarm)
            old_timer = signal.setitimer(signal.ITIMER_REAL, open_timeout)
        try:
            serial_port = serial_module.Serial(
                port,
                baud,
                timeout=0.1,
                write_timeout=2,
                dsrdtr=False,
                rtscts=False,
            )
            serial_port.dtr = True
            serial_port.rts = False
            return serial_port
        except SerialOpenTimeout as exc:
            raise RuntimeError(f"serial open timed out after {open_timeout:.0f}s on {port}") from exc
        except (OSError, serial_module.SerialException) as exc:
            raise RuntimeError(f"serial open failed on {port}: {exc}") from exc
        finally:
            if old_handler is not None and old_timer is not None:
                signal.setitimer(signal.ITIMER_REAL, old_timer[0], old_timer[1])
                signal.signal(signal.SIGALRM, old_handler)

    def close(self) -> None:
        self.serial.close()

    def _read_for(self, seconds: float) -> bytes:
        chunks: list[bytes] = []
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            data = self.serial.read(4096)
            if data:
                chunks.append(data.replace(CONTROL_NOISE, b""))
        return b"".join(chunks)

    def run(self, command: str, timeout: float = 8.0) -> CommandResult:
        self.counter += 1
        ident = f"FJHCD_{self.counter:04d}"
        begin = f"__{ident}_BEGIN__"
        end = f"__{ident}_END__"
        wrapped = f"echo {begin}; {command}; rc=$?; echo {end}:$rc"

        if self.verbose:
            print(f"$ {command}", file=sys.stderr)

        self.serial.reset_input_buffer()
        self.serial.write(wrapped.encode("utf-8") + b"\r")

        raw = bytearray()
        marker_re = re.compile((re.escape(end) + r":(\d+)").encode())
        match: re.Match[bytes] | None = None
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            data = self.serial.read(4096)
            if data:
                raw.extend(data.replace(CONTROL_NOISE, b""))
                match = marker_re.search(bytes(raw))
                if match:
                    break

        timed_out = match is None
        if timed_out:
            self.serial.write(b"\x03")
            raw.extend(self._read_for(1.0))

        text = decode(bytes(raw))
        rc = int(match.group(1)) if match else None
        output = extract_payload(text, begin, end)
        if self.verbose:
            print(output.rstrip(), file=sys.stderr)
            print(f"rc={rc} timed_out={timed_out}", file=sys.stderr)
        return CommandResult(command, output, rc, timed_out)


class TelnetShell:
    def __init__(self, host: str, port: int, verbose: bool = False):
        self.host = host
        self.port = port
        self.verbose = verbose
        self.counter = 0

    def close(self) -> None:
        return None

    @staticmethod
    def _read_available(sock: socket.socket, seconds: float) -> bytes:
        chunks: list[bytes] = []
        deadline = time.monotonic() + seconds
        sock.settimeout(0.15)
        while time.monotonic() < deadline:
            try:
                data = sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
                break
            chunks.append(data.replace(CONTROL_NOISE, b""))
        return b"".join(chunks)

    @staticmethod
    def _read_until_prompt(sock: socket.socket, seconds: float) -> bytes:
        chunks: list[bytes] = []
        deadline = time.monotonic() + seconds
        sock.settimeout(0.15)
        while time.monotonic() < deadline:
            try:
                data = sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
                break
            chunks.append(data.replace(CONTROL_NOISE, b""))
            if "fj$" in decode(b"".join(chunks)):
                break
        return b"".join(chunks)

    @classmethod
    def _send_line_wait_prompt(
        cls,
        sock: socket.socket,
        line: str,
        seconds: float,
    ) -> tuple[bytes, bool]:
        sock.sendall((line + "\n").encode("utf-8"))
        data = cls._read_until_prompt(sock, seconds)
        return data, "fj$" in decode(data)

    def run(self, command: str, timeout: float = 8.0) -> CommandResult:
        self.counter += 1
        ident = f"FJHCD_{self.counter:04d}"
        begin = f"__{ident}_BEGIN__"
        end = f"__{ident}_END__"

        if self.verbose:
            print(f"$ {command}", file=sys.stderr)

        raw = bytearray()
        marker_re = re.compile((r"(?:^|\r?\n)" + re.escape(end) + r"(?:\r?\n|$)").encode())
        match: re.Match[bytes] | None = None
        timed_out = False

        try:
            with socket.create_connection((self.host, self.port), timeout=10) as sock:
                raw.extend(self._read_until_prompt(sock, 5.0))

                for line, wait in (
                    (f"echo {begin}", 3.0),
                    (command, timeout),
                    ("status", 3.0),
                    (f"echo {end}", 3.0),
                ):
                    data, saw_prompt = self._send_line_wait_prompt(sock, line, wait)
                    raw.extend(data.replace(CONTROL_NOISE, b""))
                    if not saw_prompt:
                        timed_out = True
                        break

                match = marker_re.search(bytes(raw))
                try:
                    if timed_out:
                        sock.sendall(b"\x03\n")
                        raw.extend(self._read_available(sock, 1.0))
                    sock.sendall(b"exit\n")
                except OSError:
                    pass
        except OSError as exc:
            return CommandResult(command, f"telnet error: {exc}", None, False)

        text = decode(bytes(raw))
        output, rc = extract_fruitjam_shell_payload(text, begin, end)
        if self.verbose:
            print(output.rstrip(), file=sys.stderr)
            print(f"rc={rc} timed_out={timed_out or match is None}", file=sys.stderr)
        return CommandResult(command, output, rc, timed_out or match is None)


def http_api_url(host: str, explicit_url: str | None) -> str:
    if explicit_url:
        return explicit_url
    return f"http://{host}/cgi-bin/fruitjam.cgi"


def bool_int(value) -> int:
    return 1 if bool(value) else 0


def usbhost_status_from_json(data: dict) -> str:
    device = str(data.get("device") or "unknown")
    probe_summary = str(data.get("probe_summary") or "")
    lines = [
        f"usbhost device {device}",
        "usbhost hcd registered "
        f"{bool_int(data.get('hcd_registered'))} manual-start "
        f"{bool_int(data.get('hcd_manual_start'))} faulted "
        f"{bool_int(data.get('hcd_faulted'))} ep0-failures "
        f"{int(data.get('hcd_ep0_failures') or 0)} delay-ms "
        f"{int(data.get('hcd_delay_ms') or 0)} reset-settle-ms "
        f"{int(data.get('hcd_port_reset_settle_ms') or 0)} reset-sof-frames "
        f"{int(data.get('hcd_port_reset_sof_frames') or 0)} data-ack-tail-drain-us "
        f"{int(data.get('hcd_data_ack_tail_drain_us') or 0)} sof "
        f"{bool_int(data.get('hcd_sof'))}",
        "usbhost pio-debug index "
        f"{int(data.get('pio_index') or 0)} sm-tx "
        f"{int(data.get('sm_tx') or 0)} sm-rx "
        f"{int(data.get('sm_rx') or 0)} sm-eop "
        f"{int(data.get('sm_eop') or 0)} clk-sys-hz "
        f"{int(data.get('clk_sys_hz') or 0)}",
        "usbhost packets "
        f"{int(data.get('packets') or 0)} tx-errors "
        f"{int(data.get('tx_errors') or 0)} last-tx-result "
        f"{int(data.get('last_tx_result') or 0)} len "
        f"{int(data.get('last_tx_len') or 0)}",
        "usbhost dma present "
        f"{int(data.get('dma') or 0)} channel "
        f"{int(data.get('tx_dma_channel') or 0)} packets "
        f"{int(data.get('tx_dma_packets') or 0)} last-result 0 ctrl 0x00000000",
        "usbhost rx-attempts "
        f"{int(data.get('rx_attempts') or 0)} rx-errors "
        f"{int(data.get('rx_errors') or 0)} last-rx-result "
        f"{int(data.get('last_rx_result') or 0)} pid "
        f"0x{int(data.get('last_rx_pid') or 0):02x} len "
        f"{int(data.get('last_rx_len') or 0)}",
        "usbhost next pio-packet-io first=boot-protocol-keyboard",
    ]
    if probe_summary:
        lines.append(f"usbhost probe-summary {probe_summary}")
    return "\n".join(lines)


class HttpApiShell:
    def __init__(self, host: str, url: str | None, verbose: bool = False):
        self.url = http_api_url(host, url)
        self.verbose = verbose
        self.last_status: dict | None = None

    def close(self) -> None:
        return None

    def _api(self, cmd: str, timeout: float) -> dict:
        sep = "&" if "?" in self.url else "?"
        url = self.url + sep + urlencode({"action": "usbhost", "cmd": cmd})
        if self.verbose:
            print(f"$ GET {url}", file=sys.stderr)
        with urlopen(url, timeout=timeout) as response:
            payload = response.read().decode("utf-8", "replace")
        data = json.loads(payload)
        if not isinstance(data, dict):
            raise RuntimeError("HTTP API returned non-object JSON")
        self.last_status = data
        return data

    def _registered_root_hub(self) -> bool:
        return bool(self.last_status and self.last_status.get("hcd_registered"))

    def run(self, command: str, timeout: float = 8.0) -> CommandResult:
        try:
            if command == f"echo {SHELL_PROBE_MARKER}":
                self._api("status", timeout)
                return CommandResult(command, SHELL_PROBE_MARKER, 0, False)
            if command == KERNEL_RELEASE_CMD:
                return CommandResult(
                    command,
                    "6.15.0",
                    0,
                    False,
                )
            if command == "fruitjam-usbhost status":
                data = self._api("status", timeout)
                return CommandResult(command, usbhost_status_from_json(data), 0, False)
            if command == HCD_START_CMD:
                data = self._api("hcd-start", timeout)
                return CommandResult(command, usbhost_status_from_json(data),
                                     0 if data.get("ok") else 1, False)
            if command == DEV_INPUT_CMD:
                data = self._api("dev-input", timeout)
                return CommandResult(command, str(data.get("output") or ""),
                                     0 if data.get("ok") else int(data.get("exit") or 1),
                                     False)
            if command == INPUT_REGISTRY_CMD:
                data = self._api("input-registry", timeout)
                return CommandResult(command, str(data.get("output") or ""),
                                     0 if data.get("ok") else int(data.get("exit") or 1),
                                     False)
            if command == USB_DEVICE_REGISTRY_CMD:
                data = self._api("usb-devices", timeout)
                return CommandResult(command, str(data.get("output") or ""),
                                     0 if data.get("ok") else int(data.get("exit") or 1),
                                     False)
            if command == "cat /sys/bus/usb/devices/usb1/product":
                output = "Fruit Jam RP2350 PIO USB host" if self._registered_root_hub() else ""
                return CommandResult(command, output, 0 if output else 1, False)
            if command == "cat /sys/bus/usb/devices/usb1/speed":
                output = "12" if self._registered_root_hub() else ""
                return CommandResult(command, output, 0 if output else 1, False)
            if command == "cat /sys/bus/usb/devices/usb1/maxchild":
                output = ROOT_HUB_MAXCHILD if self._registered_root_hub() else ""
                return CommandResult(command, output, 0 if output else 1, False)
            if command == "ls /sys/bus/usb/devices":
                output = "usb1" if self._registered_root_hub() else ""
                return CommandResult(command, output, 0 if output else 1, False)
            return CommandResult(command, "unsupported HTTP transport command", 127, False)
        except (OSError, RuntimeError, json.JSONDecodeError, URLError) as exc:
            return CommandResult(command, f"HTTP API error: {exc}", None, False)


class FakeShell:
    def run(self, command: str, timeout: float = 8.0) -> CommandResult:
        del timeout
        if command == f"echo {SHELL_PROBE_MARKER}":
            output = SHELL_PROBE_MARKER
        elif command == KERNEL_RELEASE_CMD:
            output = "6.15.0"
        elif command == "fruitjam-usbhost status":
            output = (
                "usbhost device full-speed-device\n"
                "usbhost hcd registered 1 manual-start 1 faulted 0 ep0-failures 0 delay-ms 8000 reset-settle-ms 500 reset-sof-frames 25 data-ack-tail-drain-us 4 sof 1\n"
                "usbhost pio-debug index 0 sm-tx 0 sm-rx 1 sm-eop 2 clk-sys-hz 252000000\n"
                "usbhost packets 0 tx-errors 0 last-tx-result 0 len 0\n"
                "usbhost dma present 1 channel 9 packets 0 last-result 0 ctrl 0x00000000\n"
                "usbhost rx-attempts 0 rx-errors 0 last-rx-result 0 pid 0x00 len 0\n"
                "usbhost next pio-packet-io first=boot-protocol-keyboard"
            )
        elif command == HCD_START_CMD:
            output = "HCD start already registered"
        elif command == HCD_STATUS_CMD:
            output = (
                "usbhost device full-speed-device\n"
                "usbhost hcd registered 1 manual-start 1 faulted 0 ep0-failures 0 delay-ms 8000 reset-settle-ms 500 reset-sof-frames 25 data-ack-tail-drain-us 4 sof 1\n"
                "usbhost pio-debug index 0 sm-tx 0 sm-rx 1 sm-eop 2 clk-sys-hz 252000000\n"
                "usbhost packets 42 tx-errors 0 last-tx-result 0 len 12\n"
                "usbhost dma present 1 channel 9 packets 42 last-result 0 ctrl 0x00000000\n"
                "usbhost rx-attempts 18 rx-errors 0 last-rx-result 0 pid 0x4b len 8\n"
                "usbhost next pio-packet-io first=boot-protocol-keyboard"
            )
        elif command == "cat /sys/bus/usb/devices/usb1/product":
            output = "Fruit Jam RP2350 PIO USB host"
        elif command == "cat /sys/bus/usb/devices/usb1/speed":
            output = "12"
        elif command == "cat /sys/bus/usb/devices/usb1/maxchild":
            output = ROOT_HUB_MAXCHILD
        elif command == "ls /sys/bus/usb/devices":
            output = "1-0:1.0 1-1 1-1:1.0 1-1.1 1-1.1:1.0 1-1.2 1-1.2:1.0 usb1"
        elif command == DEV_INPUT_CMD:
            output = "inputdev event0\ninputdev event1\ninputdev js0"
        elif command == INPUT_REGISTRY_CMD:
            output = (
                "I: Bus=0003 Vendor=046d Product=c52b Version=0111\n"
                "N: Name=\"Logitech USB Receiver\"\n"
                "H: Handlers=sysrq kbd event0\n\n"
                "I: Bus=0003 Vendor=045e Product=0291 Version=0100\n"
                "N: Name=\"Xbox 360 Wireless Receiver (XBOX)\"\n"
                "H: Handlers=event1 js0\n"
            )
        elif command == USB_DEVICE_REGISTRY_CMD:
            output = (
                "usbhost usb-devices begin\n"
                "usbdev usb1 -:- manufacturer=- product=Fruit Jam RP2350 PIO USB host speed=12 maxchild=1\n"
                "usbdev 1-1 -:- manufacturer=- product=CH334F USB2.0 Hub speed=12 maxchild=4\n"
                "usbdev 1-1.1 046d:c52b manufacturer=Logitech product=USB Receiver speed=12 maxchild=0\n"
                "usbdev 1-1.2 045e:0291 manufacturer=Microsoft product=Xbox 360 Wireless Receiver (XBOX) speed=12 maxchild=0\n"
                "usbhost usb-devices count 4\n"
                "usbhost usb-devices end"
            )
        else:
            output = "unexpected command"
            return CommandResult(command, output, 127, False)
        return CommandResult(command, output, 0, False)

    def close(self) -> None:
        return None


def first_line(text: str) -> str:
    for line in text.splitlines():
        if line.strip():
            return line.strip()
    return "(no output)"


def clean_output(result: CommandResult) -> bool:
    bad_fragments = (
        "Bad address",
        "HTTP API error:",
        "Segmentation fault",
        "page allocation failure",
        "Allocation of length",
        "Traceback",
        "telnet error:",
    )
    return not any(fragment in result.output for fragment in bad_fragments)


def check_command(shell, name: str, command: str, timeout: float = 8.0) -> Check:
    result = shell.run(command, timeout=timeout)
    if result.timed_out:
        return Check(name, False, f"timeout after {timeout:.0f}s", result.output)
    if result.rc != 0 or not clean_output(result):
        return Check(name, False, f"rc={result.rc} {first_line(result.output)}", result.output)
    return Check(name, True, first_line(result.output), result.output)


def collect_command(shell, name: str, command: str, timeout: float = 8.0) -> Check:
    result = shell.run(command, timeout=timeout)
    if result.timed_out:
        return Check(name, False, f"timeout after {timeout:.0f}s", result.output)
    if result.rc is None or not clean_output(result):
        return Check(name, False, f"rc={result.rc} {first_line(result.output)}", result.output)
    detail = first_line(result.output)
    if result.rc != 0:
        detail = f"rc={result.rc} {detail}"
        return Check(name, False, detail, result.output)
    return Check(name, True, detail, result.output)


def check_by_name(checks: list[Check], name: str) -> Check | None:
    return next((c for c in checks if c.name == name), None)


def check_output_if_ok(checks: list[Check], name: str) -> str:
    check = check_by_name(checks, name)
    return check.output if check and check.ok else ""


def input_blocks(registry: str) -> list[str]:
    return [block for block in re.split(r"\n\s*\n", registry) if block.strip()]


def handlers_line(block: str) -> str:
    for line in block.splitlines():
        if line.startswith("H:"):
            return line
    return ""


def block_name(block: str) -> str:
    for line in block.splitlines():
        if line.startswith("N:"):
            return line
    return ""


def logitech_usb_present(usb_registry: str, input_registry: str) -> bool:
    haystack = f"{usb_registry}\n{input_registry}".lower()
    return "046d:" in haystack or "logitech" in haystack


def xbox_usb_present(usb_registry: str, input_registry: str) -> bool:
    haystack = f"{usb_registry}\n{input_registry}".lower()
    return any(
        needle in haystack
        for needle in (
            "045e:0291",
            "045e:028e",
            "045e:0719",
            "xbox",
            "x-box",
            "xpad",
        )
    )


def external_hub_present(usb_registry: str) -> bool:
    for line in usb_registry.splitlines():
        lower = line.lower()
        if not line.startswith("usbdev "):
            continue
        if "usbdev usb1 " in lower:
            continue
        if "maxchild=4" in lower:
            return True
        if "ch334" in lower or "usb2.0 hub" in lower or "usb 2.0 hub" in lower:
            return True
    return False


def keyboard_input_present(dev_input: str, input_registry: str) -> bool:
    if "event" not in dev_input:
        return False
    for block in input_blocks(input_registry):
        handlers = handlers_line(block).lower()
        name = block_name(block).lower()
        if "kbd" in handlers and "event" in handlers:
            return True
        if "keyboard" in name and "event" in handlers:
            return True
    return False


def xpad_input_present(dev_input: str, input_registry: str) -> bool:
    for block in input_blocks(input_registry):
        handlers = handlers_line(block).lower()
        name = block_name(block).lower()
        if ("xbox" in name or "x-box" in name or "xpad" in name) and (
            "js" in handlers or "event" in handlers
        ):
            return True
    return "js" in dev_input and xbox_usb_present("", input_registry)


def hcd_not_faulted(status: str) -> bool:
    if re.search(r"\bhcd[-_ ]fault(?:ed)?\b", status.lower()):
        if re.search(r"faulted\s+0\b", status.lower()):
            return True
        return False
    match = re.search(r"\bfaulted\s+(\d+)\b", status.lower())
    return not match or match.group(1) == "0"


def wili8jam_config_present(status: str) -> bool:
    return all(
        needle in status
        for needle in (
            "pio-debug index 0",
            "clk-sys-hz 252000000",
            "dma present 1 channel 9",
        )
    )


def hcd_service_window_present(status: str) -> bool:
    return (
        "manual-start 1" in status
        and "delay-ms 8000" in status
        and "reset-settle-ms 500" in status
        and "reset-sof-frames 25" in status
    )


def root_hub_port_count_ok(maxchild: str) -> bool:
    return maxchild.strip() == ROOT_HUB_MAXCHILD


def run_checks(shell, allow_root_only: bool) -> list[Check]:
    checks: list[Check] = []

    preflight = check_command(shell, "shell preflight", f"echo {SHELL_PROBE_MARKER}", timeout=5)
    checks.append(Check(
        preflight.name,
        preflight.ok and SHELL_PROBE_MARKER in preflight.output,
        "command markers round-tripped" if preflight.ok else preflight.detail,
        preflight.output,
    ))
    if not checks[-1].ok:
        return checks

    checks.append(check_command(shell, "kernel", KERNEL_RELEASE_CMD, timeout=5))
    checks.append(check_command(shell, "usbhost bridge pre-start", "fruitjam-usbhost status", timeout=10))
    checks.append(check_command(shell, "usbhost hcd start", HCD_START_CMD, timeout=15))
    if checks[-1].ok:
        time.sleep(2)
    checks.append(check_command(shell, "usbhost hcd status", HCD_STATUS_CMD, timeout=10))
    checks.append(check_command(
        shell,
        "root hub product",
        "cat /sys/bus/usb/devices/usb1/product",
        timeout=5,
    ))
    checks.append(check_command(
        shell,
        "root hub speed",
        "cat /sys/bus/usb/devices/usb1/speed",
        timeout=5,
    ))
    root_ports = check_command(
        shell,
        "root hub ports",
        "cat /sys/bus/usb/devices/usb1/maxchild",
        timeout=5,
    )
    if root_ports.ok and not root_hub_port_count_ok(root_ports.output):
        root_ports = Check(
            root_ports.name,
            False,
            f"expected {ROOT_HUB_MAXCHILD} upstream port, got {first_line(root_ports.output)}",
            root_ports.output,
        )
    checks.append(root_ports)
    checks.append(check_command(
        shell,
        "usb sysfs devices",
        "ls /sys/bus/usb/devices",
        timeout=5,
    ))
    checks.append(collect_command(
        shell,
        "input devices",
        DEV_INPUT_CMD,
        timeout=5,
    ))
    checks.append(collect_command(
        shell,
        "input registry",
        INPUT_REGISTRY_CMD,
        timeout=5,
    ))
    checks.append(collect_command(
        shell,
        "usb device registry",
        USB_DEVICE_REGISTRY_CMD,
        timeout=8,
    ))

    pre_status = check_output_if_ok(checks, "usbhost bridge pre-start")
    status = check_output_if_ok(checks, "usbhost hcd status")
    if not status:
        status = check_output_if_ok(checks, "usbhost hcd start")
    if not status:
        status = pre_status
    product = check_output_if_ok(checks, "root hub product")
    root_ports = check_output_if_ok(checks, "root hub ports")
    sysfs = check_output_if_ok(checks, "usb sysfs devices")
    dev_input = check_output_if_ok(checks, "input devices")
    registry = check_output_if_ok(checks, "input registry")
    usb_registry = check_output_if_ok(checks, "usb device registry")
    status_ok = bool(status and "usbhost hcd registered" in status)
    root_hub_ok = (
        "Fruit Jam RP2350 PIO USB host" in product
        and root_hub_port_count_ok(root_ports)
        and "usb1" in sysfs
    )

    checks.append(Check(
        "hcd not faulted",
        status_ok and hcd_not_faulted(status),
        "HCD EP0 fault latch is clear"
        if status_ok and hcd_not_faulted(status)
        else "HCD status unavailable or EP0 fault latch is set",
        status,
    ))
    checks.append(Check(
        "wili8jam electrical config",
        status_ok and wili8jam_config_present(status),
        "PIO0, 252 MHz clk_sys, DMA9"
        if status_ok and wili8jam_config_present(status)
        else "PIO/clock/DMA status unavailable or mismatch",
        status,
    ))
    checks.append(Check(
        "hcd service window",
        status_ok and hcd_service_window_present(pre_status + "\n" + status),
        "manual HCD start and reset settle configured"
        if status_ok and hcd_service_window_present(pre_status + "\n" + status)
        else "manual HCD start/reset settle unavailable or not reported",
        pre_status + "\n" + status,
    ))
    checks.append(Check(
        "hcd root hub",
        root_hub_ok,
        "usb1 root hub registered" if root_hub_ok else "usb1 missing or unavailable",
        product + "\n" + sysfs,
    ))
    checks.append(Check(
        "external usb packets",
        status_ok and "last-rx-result 0" in status and "last-rx-result -110" not in status,
        "PIO RX has external packet evidence"
        if status_ok and "last-rx-result 0" in status and "last-rx-result -110" not in status
        else "PIO RX packet evidence unavailable or failing",
        status,
    ))
    external_hub_ok = external_hub_present(usb_registry)
    checks.append(Check(
        "external hub usb",
        external_hub_ok or (allow_root_only and root_hub_ok),
        "four-port external hub enumerated"
        if external_hub_ok
        else "external four-port hub not seen" if not allow_root_only else "root-hub-only mode",
        usb_registry,
    ))
    logitech_ok = logitech_usb_present(usb_registry, registry)
    xbox_ok = xbox_usb_present(usb_registry, registry)
    keyboard_ok = keyboard_input_present(dev_input, registry)
    xpad_ok = xpad_input_present(dev_input, registry)
    checks.append(Check(
        "logitech receiver usb",
        logitech_ok or (allow_root_only and root_hub_ok),
        "Logitech receiver enumerated"
        if logitech_ok
        else "Logitech receiver not seen" if not allow_root_only else "root-hub-only mode",
        usb_registry + "\n" + registry,
    ))
    checks.append(Check(
        "hid keyboard input",
        keyboard_ok or (allow_root_only and root_hub_ok),
        "keyboard event node registered"
        if keyboard_ok
        else "keyboard event node missing" if not allow_root_only else "root-hub-only mode",
        dev_input + "\n" + registry,
    ))
    checks.append(Check(
        "xbox receiver usb",
        xbox_ok or (allow_root_only and root_hub_ok),
        "Xbox/xpad USB device enumerated"
        if xbox_ok
        else "Xbox/xpad USB device not seen" if not allow_root_only else "root-hub-only mode",
        usb_registry + "\n" + registry,
    ))
    checks.append(Check(
        "xpad gamepad input",
        xpad_ok or (allow_root_only and root_hub_ok),
        "xpad input node registered"
        if xpad_ok
        else "xpad input node missing" if not allow_root_only else "root-hub-only mode",
        dev_input + "\n" + registry,
    ))
    return checks


def print_report(checks: list[Check], verbose: bool) -> int:
    width = max(len(c.name) for c in checks) if checks else 0
    failed = 0
    for check in checks:
        status = "PASS" if check.ok else "FAIL"
        if not check.ok:
            failed += 1
        print(f"{status:4} {check.name:<{width}}  {check.detail}")
        if verbose and check.output:
            for line in check.output.splitlines():
                print(f"      {line}")
    print(f"\n{sum(c.ok for c in checks)} passed, {failed} failed")
    return 1 if failed else 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--transport", choices=("auto", "cdc", "telnet", "http"), default="auto")
    parser.add_argument("--port", default=DEFAULT_PORT, help="CDC serial port")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="CDC serial baud")
    parser.add_argument("--serial-open-timeout", type=float, default=5.0)
    parser.add_argument("--telnet-host", default=DEFAULT_TELNET_HOST,
                        help="AirLift telnet host/IP")
    parser.add_argument("--telnet-port", type=int, default=DEFAULT_TELNET_PORT)
    parser.add_argument("--http-host", default=DEFAULT_HTTP_HOST,
                        help="AirLift HTTP host/IP for --transport http")
    parser.add_argument("--http-url", default=DEFAULT_HTTP_URL,
                        help="full Fruit Jam API URL for --transport http")
    parser.add_argument("--allow-root-only", action="store_true",
                        help="do not fail only because no downstream input node appeared")
    parser.add_argument("--self-test", action="store_true",
                        help="run the smoke logic against an in-process fake shell")
    parser.add_argument("-v", "--verbose", action="store_true")
    return parser.parse_args()


def make_shell(args: argparse.Namespace, transport: str):
    if args.self_test:
        return FakeShell()
    if transport == "http":
        return HttpApiShell(args.http_host, args.http_url, args.verbose)
    if transport == "telnet":
        if not args.telnet_host:
            raise RuntimeError("--telnet-host is required")
        return TelnetShell(args.telnet_host, args.telnet_port, args.verbose)
    if transport == "cdc":
        return SerialShell(args.port, args.baud, args.serial_open_timeout, args.verbose)
    raise RuntimeError(f"unsupported transport {transport}")


def auto_transports(args: argparse.Namespace) -> list[str]:
    transports: list[str] = []
    if args.http_url or args.http_host:
        transports.append("http")
    if args.telnet_host:
        transports.append("telnet")
    transports.append("cdc")
    return transports


def main() -> int:
    args = parse_args()
    transports = auto_transports(args) if args.transport == "auto" else [args.transport]
    preflight_failures: list[Check] = []
    for transport in transports:
        shell = None
        try:
            shell = make_shell(args, transport)
            checks = run_checks(shell, args.allow_root_only)
            if checks and checks[0].ok:
                return print_report(checks, args.verbose)
            detail = checks[0].detail if checks else "no checks ran"
            output = checks[0].output if checks else ""
            preflight_failures.append(Check(
                f"{transport} preflight",
                False,
                detail,
                output,
            ))
        except KeyboardInterrupt:
            print("\nInterrupted", file=sys.stderr)
            return 130
        except RuntimeError as exc:
            preflight_failures.append(Check(f"{transport} preflight", False, str(exc)))
        except OSError as exc:
            preflight_failures.append(Check(
                f"{transport} preflight",
                False,
                f"open failed: {exc}",
            ))
        finally:
            if shell is not None:
                shell.close()
    return print_report(preflight_failures, args.verbose)


if __name__ == "__main__":
    raise SystemExit(main())
