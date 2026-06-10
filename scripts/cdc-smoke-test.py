#!/usr/bin/env python3
"""Run Fruit Jam Linux smoke tests over the USB CDC console."""

from __future__ import annotations

import argparse
import os
import re
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Callable, Iterable

try:
    import serial
except ImportError as exc:  # pragma: no cover - host dependency guard
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc


PROMPT_RE = re.compile(rb"(?:^|\r|\n)/ # ")
CONTROL_NOISE = b"\x10\x01\x01"


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


class CdcShell:
    def __init__(self, port: str, baud: int, verbose: bool = False):
        self.port = port
        self.baud = baud
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

    def request_reboot(self) -> None:
        self.serial.reset_input_buffer()
        self.serial.write(b"\rsync; fruitjamctl bootsel\r")
        self.serial.flush()

    def _read_for(self, seconds: float) -> bytes:
        end = time.monotonic() + seconds
        chunks: list[bytes] = []
        while time.monotonic() < end:
            data = self.serial.read(4096)
            if data:
                chunks.append(data.replace(CONTROL_NOISE, b""))
        return b"".join(chunks)

    def sync(self, timeout: float = 8.0) -> str:
        self.serial.reset_input_buffer()
        self.serial.write(b"\r")
        deadline = time.monotonic() + timeout
        buf = bytearray()
        while time.monotonic() < deadline:
            data = self.serial.read(4096)
            if data:
                buf.extend(data.replace(CONTROL_NOISE, b""))
                if PROMPT_RE.search(bytes(buf)):
                    return decode(bytes(buf))
            else:
                self.serial.write(b"\r")
                time.sleep(0.2)
        raise TimeoutError(f"no Fruit Jam shell prompt on {self.port}")

    def run(self, command: str, timeout: float = 12.0) -> CommandResult:
        self.counter += 1
        ident = f"FJSMOKE_{self.counter:04d}"
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


def decode(data: bytes) -> str:
    return data.decode("utf-8", "replace").replace("\x10\x01\x01", "")


def extract_payload(text: str, begin: str, end: str) -> str:
    # The CDC shell echoes the full wrapped command before running it, so the
    # marker appears once in the echo and once in real command output.
    start = text.rfind(begin)
    if start >= 0:
        text = text[start + len(begin) :]
    finish = text.find(end)
    if finish >= 0:
        text = text[:finish]
    lines = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped == "/ #":
            continue
        if stripped.startswith("/ # "):
            stripped = stripped[4:]
        lines.append(stripped)
    return "\n".join(lines).strip()


def pass_if(
    shell: CdcShell,
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


def skip(name: str, reason: str) -> TestResult:
    return TestResult(name, "SKIP", reason)


def first_line(text: str) -> str:
    for line in text.splitlines():
        if line.strip():
            return line.strip()
    return "(no output)"


def contains(*needles: str) -> Callable[[CommandResult], bool]:
    return lambda result: result.rc == 0 and clean_output(result) and all(n in result.output for n in needles)


def rc_zero(result: CommandResult) -> bool:
    return result.rc == 0 and clean_output(result)


def rc_any_with_output(result: CommandResult) -> bool:
    return result.rc is not None and clean_output(result) and bool(result.output.strip())


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


def shell_quote(value: str) -> str:
    return shlex.quote(value)


def core_tests(shell: CdcShell, args: argparse.Namespace) -> Iterable[TestResult]:
    yield pass_if(shell, "cdc shell", "echo fruitjam-cdc-ok", contains("fruitjam-cdc-ok"))
    yield pass_if(shell, "kernel", "cat /proc/version", rc_zero)
    yield pass_if(shell, "tool inventory", tool_inventory_command(), inventory_ok)
    yield pass_if(shell, "berry expression", "berry -e 'print(\"berry-cdc-ok\")'", contains("berry-cdc-ok"))
    yield pass_if(shell, "board status", "fruitjamctl status", contains("red-led", "button1"))
    yield pass_if(shell, "buttons", "fruitjam-buttons status", contains("button1", "button2", "button3"))
    yield pass_if(shell, "i2c scan", "fruitjam-i2c scan", rc_zero, timeout=20)
    yield pass_if(
        shell,
        "neopixel device",
        "printf 'clear\\nwrite\\n' > /dev/neopixels",
        rc_zero,
        detail="cleared /dev/neopixels",
    )
    if args.audio:
        yield pass_if(
            shell,
            "rtttl audio",
            "fruitjam-rtttl 'smoke:d=16,o=5,b=160:c,e,g,c6'",
            contains("played"),
            timeout=20,
        )
    else:
        yield skip("rtttl audio", "use --audio to play a short tune")


def service_tests(shell: CdcShell) -> Iterable[TestResult]:
    yield pass_if(shell, "service status", "fruitjam-services status", rc_any_with_output)
    yield pass_if(
        shell,
        "http loopback",
        "wget -O - http://127.0.0.1/cgi-bin/fruitjam.cgi?action=status",
        contains('"ok":true'),
        timeout=20,
    )
    yield pass_if(
        shell,
        "telnet loopback",
        "printf 'echo telnet-ok\\nexit\\n' | telnet 127.0.0.1 23",
        contains("telnet-ok"),
        timeout=20,
    )
    yield pass_if(
        shell,
        "tftp loopback",
        "tftp -g -r README.txt -l /tmp/tftp-smoke.txt 127.0.0.1; cat /tmp/tftp-smoke.txt",
        contains("Fruit Jam TFTP area"),
        timeout=20,
    )
    yield pass_if(
        shell,
        "ftp banner",
        "nc 127.0.0.1 21 < /dev/null",
        contains("220"),
        timeout=10,
    )


def airlift_tests(shell: CdcShell, args: argparse.Namespace) -> Iterable[TestResult]:
    yield pass_if(shell, "airlift probe", "airliftctl probe", contains("firmware", "mac"), timeout=35)
    yield pass_if(
        shell,
        "airlift scan",
        "airliftctl scan",
        lambda r: r.rc == 0 and clean_output(r) and "networks" in r.output,
        timeout=35,
    )

    ssid = args.ssid or os.environ.get("FJ_WIFI_SSID")
    password = args.password or os.environ.get("FJ_WIFI_PASSWORD")
    if not ssid or not password:
        yield skip("airlift join", "set FJ_WIFI_SSID/FJ_WIFI_PASSWORD or pass --ssid/--password")
        yield skip("airlift tcp-get", "requires WiFi join")
        return

    join_cmd = f"airliftctl join {shell_quote(ssid)} {shell_quote(password)}"
    yield pass_if(shell, "airlift join", join_cmd, contains("ip "), timeout=45)
    yield pass_if(
        shell,
        "airlift tcp-get",
        "airliftctl tcp-get example.com /",
        lambda r: r.rc == 0 and clean_output(r) and ("HTTP/" in r.output or "Example Domain" in r.output),
        timeout=45,
    )


def tool_inventory_command() -> str:
    tools = [
        "airliftctl",
        "berry",
        "fruitjamctl",
        "fruitjam-i2c",
        "fruitjam-rtttl",
        "fruitjam-services",
        "fruitjam-buttons",
        "wget",
        "telnet",
        "nc",
        "tftp",
        "vi",
    ]
    checks = []
    for tool in tools:
        quoted = shell_quote(tool)
        checks.append(
            f"if command -v {quoted} >/dev/null 2>&1; then echo tool:{tool}:ok; else echo tool:{tool}:missing; fi"
        )
    return " ; ".join(checks)


def inventory_ok(result: CommandResult) -> bool:
    if result.rc != 0:
        return False
    missing = [line for line in result.output.splitlines() if line.endswith(":missing")]
    return not missing


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
    passed = sum(1 for r in results if r.status == "PASS")
    skipped = sum(1 for r in results if r.status == "SKIP")
    print(f"\n{passed} passed, {failed} failed, {skipped} skipped")
    return 1 if failed else 0


def wait_for_port_cycle(port: str, timeout: float = 45.0) -> None:
    deadline = time.monotonic() + timeout
    saw_absent = False

    while time.monotonic() < deadline:
        exists = os.path.exists(port)
        if not exists:
            saw_absent = True
        if saw_absent and exists:
            time.sleep(2.0)
            return
        time.sleep(0.25)
    raise TimeoutError(f"{port} did not cycle after reboot")


def reboot_from_bootsel(port: str, picotool: str, timeout: float = 45.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        info = subprocess.run(
            [picotool, "info", "-a"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        if info.returncode == 0:
            break
        time.sleep(0.5)
    else:
        raise TimeoutError("RP2350 BOOTSEL did not appear")

    reboot = subprocess.run(
        [picotool, "reboot", "-a", "-c", "riscv"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if reboot.returncode != 0:
        raise RuntimeError(reboot.stdout.strip() or "picotool reboot failed")
    wait_for_port_cycle(port, timeout=timeout)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=os.environ.get("FJ_CDC_PORT", "/dev/tty.usbmodem1101"))
    parser.add_argument("--baud", type=int, default=int(os.environ.get("FJ_CDC_BAUD", "115200")))
    parser.add_argument(
        "--env-file",
        default=".fruitjam.env",
        help="optional KEY=VALUE or export KEY=VALUE file for WiFi credentials",
    )
    parser.add_argument("--ssid", help="WiFi SSID for AirLift join test")
    parser.add_argument("--password", help="WiFi passphrase for AirLift join test")
    parser.add_argument("--audio", action="store_true", help="play a short RTTTL tune")
    parser.add_argument("--skip-airlift", action="store_true", help="skip AirLift and WiFi tests")
    parser.add_argument("--skip-services", action="store_true", help="skip loopback network service tests")
    parser.add_argument(
        "--no-phase-reboot",
        action="store_true",
        help="do not reboot between AirLift and loopback service test phases",
    )
    parser.add_argument(
        "--picotool",
        default=os.environ.get("PICOTOOL", "/Users/fred/.pico-sdk/picotool/2.1.1/picotool/picotool"),
        help="picotool path used to leave BOOTSEL during phase reboot",
    )
    parser.add_argument("-v", "--verbose", action="store_true", help="show command output")
    return parser.parse_args()


def load_env_file(path: str) -> None:
    if not path or not os.path.exists(path):
        return

    with open(path, "r", encoding="utf-8") as env_file:
        for raw in env_file:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("export "):
                line = line[len("export ") :].strip()
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip().strip('"').strip("'")
            if key and key not in os.environ:
                os.environ[key] = value


def main() -> int:
    args = parse_args()
    load_env_file(args.env_file)
    shell = CdcShell(args.port, args.baud, verbose=args.verbose)
    try:
        shell.sync()
        results: list[TestResult] = []
        results.extend(core_tests(shell, args))
        if args.skip_airlift:
            results.append(skip("airlift", "disabled by --skip-airlift"))
        else:
            results.extend(airlift_tests(shell, args))

        if not args.skip_services and not args.skip_airlift and not args.no_phase_reboot:
            try:
                shell.request_reboot()
                shell.close()
                reboot_from_bootsel(args.port, args.picotool)
                shell = CdcShell(args.port, args.baud, verbose=args.verbose)
                shell.sync(timeout=20.0)
                results.append(TestResult("phase reboot", "PASS", "BOOTSEL -> RISC-V reboot before service tests"))
            except Exception as exc:
                results.append(TestResult("phase reboot", "FAIL", str(exc)))
                return print_report(results, args.verbose)

        if args.skip_services:
            results.append(skip("network services", "disabled by --skip-services"))
        else:
            results.extend(service_tests(shell))
        return print_report(results, args.verbose)
    finally:
        shell.close()


if __name__ == "__main__":
    raise SystemExit(main())
