#!/usr/bin/env python3
"""Run the focused Fruit Jam USB host boot-keyboard smoke test."""

from __future__ import annotations

import argparse
import os
import re
import socket
import sys
import time
from dataclasses import dataclass
from typing import Callable, Protocol


CONTROL_NOISE = b"\x10\x01\x01"
DEFAULT_PORT = os.environ.get("FJ_CDC_PORT", "/dev/tty.usbmodem1101")
DEFAULT_BAUD = int(os.environ.get("FJ_CDC_BAUD", "115200"))
USB_KEYBOARD_SHELL_MARKER = "USBKBD_SMOKE"


@dataclass
class CommandResult:
    command: str
    output: str
    rc: int | None
    timed_out: bool


@dataclass
class TestResult:
    name: str
    status: str
    detail: str
    output: str = ""


class Shell(Protocol):
    def run(self, command: str, timeout: float = 12.0) -> CommandResult:
        ...

    def close(self) -> None:
        ...


def decode(data: bytes) -> str:
    return data.decode("utf-8", "replace").replace("\x10\x01\x01", "")


def extract_payload(text: str, begin: str, end: str) -> str:
    start = text.rfind(begin)
    if start >= 0:
        text = text[start + len(begin) :]
    finish = text.find(end)
    if finish >= 0:
        text = text[:finish]

    lines = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped == "/ #":
            continue
        if stripped.startswith("/ # "):
            stripped = stripped[4:]
        lines.append(stripped)
    return "\n".join(lines).strip()


class SerialShell:
    def __init__(self, port: str, baud: int, verbose: bool = False):
        try:
            import serial
        except ImportError as exc:  # pragma: no cover - host dependency guard
            raise SystemExit("pyserial is required for CDC mode: python3 -m pip install pyserial") from exc

        self.verbose = verbose
        self.serial = serial.Serial(
            port,
            baud,
            timeout=0.1,
            write_timeout=2,
            dsrdtr=False,
            rtscts=False,
        )
        self.counter = 0

    def close(self) -> None:
        self.serial.close()

    def _read_for(self, seconds: float) -> bytes:
        end = time.monotonic() + seconds
        chunks: list[bytes] = []
        while time.monotonic() < end:
            data = self.serial.read(4096)
            if data:
                chunks.append(data.replace(CONTROL_NOISE, b""))
        return b"".join(chunks)

    def run(self, command: str, timeout: float = 12.0) -> CommandResult:
        self.counter += 1
        ident = f"FJUSB_{self.counter:04d}"
        begin = f"__{ident}_BEGIN__"
        end = f"__{ident}_END__"
        wrapped = f"echo {begin}; {command}; rc=$?; echo {end}:$rc"

        if self.verbose:
            print(f"$ {command}", file=sys.stderr)

        self.serial.reset_input_buffer()
        self.serial.write(wrapped.encode("utf-8") + b"\r")
        self.serial.flush()

        deadline = time.monotonic() + timeout
        raw = bytearray()
        marker_re = re.compile((re.escape(end) + r":(\d+)").encode())
        match: re.Match[bytes] | None = None

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

    def run(self, command: str, timeout: float = 12.0) -> CommandResult:
        self.counter += 1
        ident = f"FJUSB_{self.counter:04d}"
        begin = f"__{ident}_BEGIN__"
        end = f"__{ident}_END__"
        wrapped = f"echo {begin}; {command}; rc=$?; echo {end}:$rc"

        if self.verbose:
            print(f"$ {command}", file=sys.stderr)

        raw = bytearray()
        marker_re = re.compile((re.escape(end) + r":(\d+)").encode())
        match: re.Match[bytes] | None = None

        try:
            with socket.create_connection((self.host, self.port), timeout=10) as sock:
                raw.extend(self._read_available(sock, 0.75))
                sock.settimeout(0.25)
                sock.sendall(wrapped.encode("utf-8") + b"\nexit\n")

                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    try:
                        data = sock.recv(4096)
                    except socket.timeout:
                        continue
                    if not data:
                        break
                    raw.extend(data.replace(CONTROL_NOISE, b""))
                    match = marker_re.search(bytes(raw))
                    if match:
                        break

                if match is None:
                    try:
                        sock.sendall(b"\x03\n")
                        raw.extend(self._read_available(sock, 1.0))
                    except OSError:
                        pass
        except OSError as exc:
            return CommandResult(command, f"telnet error: {exc}", None, False)

        text = decode(bytes(raw))
        rc = int(match.group(1)) if match else None
        output = extract_payload(text, begin, end)
        if self.verbose:
            print(output.rstrip(), file=sys.stderr)
            print(f"rc={rc} timed_out={match is None}", file=sys.stderr)
        return CommandResult(command, output, rc, match is None)


class FakeShell:
    def run(self, command: str, timeout: float = 12.0) -> CommandResult:
        del timeout
        if command == "fruitjam-usbhost status":
            output = "usbhost device full-speed-device\nusbhost next pio-packet-io first=boot-protocol-keyboard"
        elif command == "berry-run /root/berry/12-usbhost-keyboard.be":
            output = "12-usbhost-keyboard.be: ok"
        elif command == "fruitjam-usbhost kbd-find":
            output = "usbhost keyboard target addr=1 config=1 iface=0 ep=2 source=report"
        elif command.startswith("fruitjam-usbhost kbd-auto-text"):
            output = "usbhost keyboard target addr=1 config=1 iface=0 ep=2 source=nak\nabc"
        elif command.startswith("fruitjam-usbhost kbd-auto-events"):
            output = "press key=a char=a code=0x04 modifiers=0x00\nrelease key=a code=0x04"
        elif command.startswith("fruitjam-usbhost kbd-auto-shell"):
            output = (
                "USB keyboard shell; type exit to leave\n"
                "usbkbd$ echo USBKBD_SMOKE\nUSBKBD_SMOKE\nusbkbd$"
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
        "Segmentation fault",
        "page allocation failure",
        "Allocation of length",
        "Traceback",
        "not found",
    )
    return not any(fragment in result.output for fragment in bad_fragments)


def strip_usb_keyboard_noise(output: str) -> str:
    lines = []
    for line in output.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("usbhost keyboard target "):
            continue
        if stripped.startswith("usbhost keyboard "):
            continue
        if stripped.startswith("USB keyboard shell"):
            continue
        if stripped.startswith("usbkbd$"):
            continue
        lines.append(stripped)
    return "\n".join(lines)


def pass_if(
    shell: Shell,
    name: str,
    command: str,
    predicate: Callable[[CommandResult], bool],
    timeout: float = 12.0,
    detail: str | None = None,
) -> TestResult:
    result = shell.run(command, timeout=timeout)
    ok = not result.timed_out and predicate(result)
    if ok:
        return TestResult(name, "PASS", detail or first_line(result.output), result.output)
    if result.timed_out:
        return TestResult(name, "FAIL", f"timeout after {timeout:.0f}s", result.output)
    return TestResult(name, "FAIL", f"rc={result.rc} {first_line(result.output)}", result.output)


def usbhost_keyboard_tests(shell: Shell, seconds: int, require_input: bool) -> list[TestResult]:
    results: list[TestResult] = []
    results.append(pass_if(
        shell,
        "usbhost bridge status",
        "fruitjam-usbhost status",
        lambda r: r.rc == 0 and clean_output(r) and "usbhost device" in r.output,
        timeout=12,
    ))
    results.append(pass_if(
        shell,
        "usb keyboard berry helper",
        "berry-run /root/berry/12-usbhost-keyboard.be",
        lambda r: r.rc == 0 and clean_output(r) and "12-usbhost-keyboard.be: ok" in r.output,
        timeout=45,
    ))
    results.append(pass_if(
        shell,
        "usb keyboard target",
        "fruitjam-usbhost kbd-find",
        lambda r: r.rc == 0 and clean_output(r) and "usbhost keyboard target" in r.output,
        timeout=45,
    ))

    if require_input:
        print("USB keyboard text test: type a few characters on the Fruit Jam USB keyboard.", flush=True)
    results.append(pass_if(
        shell,
        "usb keyboard text",
        f"fruitjam-usbhost kbd-auto-text {seconds}",
        lambda r: (
            r.rc == 0 and clean_output(r) and
            (not require_input or bool(strip_usb_keyboard_noise(r.output)))
        ),
        timeout=seconds + 20,
        detail="live text loop saw typed characters" if require_input else "live text loop completed",
    ))

    if require_input:
        print("USB keyboard event test: press and release at least one key.", flush=True)
    results.append(pass_if(
        shell,
        "usb keyboard events",
        f"fruitjam-usbhost kbd-auto-events {seconds}",
        lambda r: (
            r.rc == 0 and clean_output(r) and (
                not require_input or "press key=" in r.output or "release key=" in r.output
            )
        ),
        timeout=seconds + 20,
        detail="live event loop saw key events" if require_input else "live event loop completed",
    ))

    if require_input:
        print(
            "USB keyboard shell test: type "
            f"`echo {USB_KEYBOARD_SHELL_MARKER}` then Enter on the Fruit Jam USB keyboard.",
            flush=True,
        )
    results.append(pass_if(
        shell,
        "usb keyboard shell",
        f"fruitjam-usbhost kbd-auto-shell {seconds}",
        lambda r: (
            r.rc == 0 and clean_output(r) and "USB keyboard shell" in r.output
            and "usbkbd$" in r.output
            and (not require_input or USB_KEYBOARD_SHELL_MARKER in r.output)
        ),
        timeout=seconds + 25,
        detail=(
            f"keyboard shell saw {USB_KEYBOARD_SHELL_MARKER}"
            if require_input else "keyboard shell loop completed"
        ),
    ))
    return results


def print_report(results: list[TestResult], verbose: bool) -> int:
    width = max(len(r.name) for r in results) if results else 0
    failed = 0
    for result in results:
        if result.status == "FAIL":
            failed += 1
        print(f"{result.status:4} {result.name:<{width}}  {result.detail}")
        if verbose and result.output:
            for line in result.output.splitlines():
                print(f"      {line}")
    print(f"\n{sum(r.status == 'PASS' for r in results)} passed, {failed} failed")
    return 1 if failed else 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--transport", choices=("auto", "cdc", "telnet"), default="auto")
    parser.add_argument("--port", default=DEFAULT_PORT, help="CDC serial port")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="CDC serial baud")
    parser.add_argument("--telnet-host", default=os.environ.get("FJ_TELNET_HOST"), help="AirLift telnet host/IP")
    parser.add_argument("--telnet-port", type=int, default=int(os.environ.get("FJ_TELNET_PORT", "23")))
    parser.add_argument("--seconds", type=int, default=int(os.environ.get("FJ_USB_KEYBOARD_SECONDS", "8")))
    parser.add_argument("--require-input", action="store_true", help="fail unless typed text/events/shell input are captured")
    parser.add_argument("--self-test", action="store_true", help="run the smoke logic against an in-process fake shell")
    parser.add_argument("-v", "--verbose", action="store_true", help="show command output")
    return parser.parse_args()


def make_shell(args: argparse.Namespace) -> Shell:
    if args.self_test:
        return FakeShell()
    if args.transport == "telnet" or (args.transport == "auto" and args.telnet_host):
        if not args.telnet_host:
            raise SystemExit("--telnet-host is required for telnet transport")
        return TelnetShell(args.telnet_host, args.telnet_port, verbose=args.verbose)
    return SerialShell(args.port, args.baud, verbose=args.verbose)


def main() -> int:
    args = parse_args()
    shell = make_shell(args)
    try:
        results = usbhost_keyboard_tests(shell, args.seconds, args.require_input)
        return print_report(results, args.verbose)
    finally:
        shell.close()


if __name__ == "__main__":
    raise SystemExit(main())
