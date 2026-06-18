#!/usr/bin/env python3
"""Run the focused Fruit Jam USB host boot-keyboard smoke test."""

from __future__ import annotations

import argparse
import os
import re
import signal
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Callable, Protocol


CONTROL_NOISE = b"\x10\x01\x01"
DEFAULT_PORT = os.environ.get("FJ_CDC_PORT", "/dev/tty.usbmodem1101")
DEFAULT_BAUD = int(os.environ.get("FJ_CDC_BAUD", "115200"))
SHELL_PROBE_MARKER = "FJ_SHELL_READY"
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
        except ImportError as exc:  # pragma: no cover - host dependency guard
            raise SystemExit("pyserial is required for CDC mode: python3 -m pip install pyserial") from exc

        self.verbose = verbose
        self._probe_serial_open(port, baud, open_timeout)
        self.serial = self._open_serial(serial, port, baud, open_timeout)
        self.counter = 0

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

    def run(self, command: str, timeout: float = 12.0) -> CommandResult:
        self.counter += 1
        ident = f"FJUSB_{self.counter:04d}"
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


class FakeShell:
    def run(self, command: str, timeout: float = 12.0) -> CommandResult:
        del timeout
        if command == f"echo {SHELL_PROBE_MARKER}":
            output = SHELL_PROBE_MARKER
        elif command == "fruitjam-usbhost status":
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


def shell_preflight(shell: Shell, timeout: float) -> TestResult:
    result = shell.run(f"echo {SHELL_PROBE_MARKER}", timeout=timeout)
    ok = (
        not result.timed_out and
        result.rc == 0 and
        clean_output(result) and
        SHELL_PROBE_MARKER in result.output
    )
    if ok:
        return TestResult("board shell preflight", "PASS", "command markers round-tripped", result.output)
    if result.timed_out:
        return TestResult("board shell preflight", "FAIL", f"timeout after {timeout:.0f}s", result.output)
    return TestResult(
        "board shell preflight",
        "FAIL",
        f"rc={result.rc} {first_line(result.output)}",
        result.output,
    )


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
    parser.add_argument("--serial-open-timeout", type=float, default=5.0, help="seconds for opening/configuring the CDC port")
    parser.add_argument("--telnet-host", default=os.environ.get("FJ_TELNET_HOST"), help="AirLift telnet host/IP")
    parser.add_argument("--telnet-port", type=int, default=int(os.environ.get("FJ_TELNET_PORT", "23")))
    parser.add_argument("--seconds", type=int, default=int(os.environ.get("FJ_USB_KEYBOARD_SECONDS", "8")))
    parser.add_argument("--shell-probe-timeout", type=float, default=5.0, help="seconds for the initial shell echo check")
    parser.add_argument("--no-shell-probe", action="store_true", help="skip the initial board shell echo check")
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
    return SerialShell(args.port, args.baud, args.serial_open_timeout, verbose=args.verbose)


def main() -> int:
    args = parse_args()
    try:
        shell = make_shell(args)
    except RuntimeError as exc:
        return print_report([TestResult("board shell preflight", "FAIL", str(exc))], args.verbose)
    try:
        results: list[TestResult] = []
        if not args.no_shell_probe:
            results.append(shell_preflight(shell, args.shell_probe_timeout))
            if results[-1].status == "FAIL":
                return print_report(results, args.verbose)
        results.extend(usbhost_keyboard_tests(shell, args.seconds, args.require_input))
        return print_report(results, args.verbose)
    except KeyboardInterrupt:
        print("\nInterrupted", file=sys.stderr)
        return 130
    finally:
        shell.close()


if __name__ == "__main__":
    raise SystemExit(main())
