#!/usr/bin/env python3
"""Run Fruit Jam Linux smoke tests over the USB CDC console."""

from __future__ import annotations

import argparse
import os
import re
import shlex
import socket
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
INBOUND_UPLOAD_NAME = "airlift_upload_smoke.txt"
INBOUND_UPLOAD_TEXT = "hello from Fruit Jam AirLift upload smoke\n"
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
    yield pass_if(
        shell,
        "berry-run script",
        "berry-run /root/berry/00-hello.be",
        contains("00-hello.be: ok"),
        timeout=20,
    )
    yield pass_if(shell, "board status", "fruitjamctl status", contains("red-led", "button1"))
    yield pass_if(shell, "buttons", "fruitjam-buttons status", contains("button1", "button2", "button3"))
    yield pass_if(shell, "i2c codec ping", "fruitjam-i2c ping 0x18", rc_zero, timeout=8)
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
        yield pass_if(
            shell,
            "wavplay sample",
            "if [ -r /mnt/sd/wavs/fruitjam-scale.wav ]; then fruitjam-wavplay --loud /mnt/sd/wavs/fruitjam-scale.wav; else echo wavplay-skip; fi",
            lambda r: r.rc == 0 and clean_output(r) and ("played /mnt/sd/wavs/fruitjam-scale.wav" in r.output or "wavplay-skip" in r.output),
            timeout=45,
        )
    else:
        yield skip("rtttl audio", "use --audio to play a short tune")
        yield skip("wavplay sample", "use --audio to play /mnt/sd/wavs/fruitjam-scale.wav when present")

    if args.skip_display:
        yield skip("dvi bounded show", "disabled by --skip-display")
    else:
        yield pass_if(
            shell,
            "dvi bounded show",
            "fruitjam-dvi dashboard",
            rc_zero,
            timeout=12,
        )


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
        "http berry list",
        "wget -O - http://127.0.0.1/cgi-bin/fruitjam.cgi?action=berry-list",
        contains('"run-all.be"'),
        timeout=20,
    )
    yield skip(
        "http berry run",
        "browser Berry execution is verified through the AirLift HTTP bridge; local wget+httpd+Berry is too memory-heavy for no-MMU",
    )
    yield pass_if(
        shell,
        "http wav list",
        "wget -O - http://127.0.0.1/cgi-bin/fruitjam.cgi?action=wav-list",
        lambda r: r.rc == 0 and clean_output(r) and '"dir":"/mnt/sd/wavs"' in r.output and '"files":[' in r.output,
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


def usbhost_keyboard_tests(shell: CdcShell, args: argparse.Namespace) -> Iterable[TestResult]:
    seconds = args.usb_keyboard_seconds
    require_input = args.usb_keyboard_require_input

    yield pass_if(
        shell,
        "usbhost bridge status",
        "fruitjam-usbhost status",
        lambda r: r.rc == 0 and clean_output(r) and "usbhost device" in r.output,
        timeout=12,
    )
    yield pass_if(
        shell,
        "usb keyboard berry helper",
        "berry-run /root/berry/12-usbhost-keyboard.be",
        lambda r: r.rc == 0 and clean_output(r) and "12-usbhost-keyboard.be: ok" in r.output,
        timeout=45,
    )
    yield pass_if(
        shell,
        "usb keyboard target",
        "fruitjam-usbhost kbd-find",
        lambda r: r.rc == 0 and clean_output(r) and "usbhost keyboard target" in r.output,
        timeout=45,
    )
    yield pass_if(
        shell,
        "usb keyboard text",
        f"fruitjam-usbhost kbd-auto-text {seconds}",
        lambda r: (
            r.rc == 0
            and clean_output(r)
            and (not require_input or bool(strip_usb_keyboard_noise(r.output)))
        ),
        timeout=seconds + 20,
        detail=(
            "live text loop completed"
            if not require_input
            else "live text loop saw typed characters"
        ),
    )
    yield pass_if(
        shell,
        "usb keyboard events",
        f"fruitjam-usbhost kbd-auto-events {seconds}",
        lambda r: (
            r.rc == 0
            and clean_output(r)
            and (
                not require_input
                or "press key=" in r.output
                or "release key=" in r.output
            )
        ),
        timeout=seconds + 20,
        detail=(
            "live event loop completed"
            if not require_input
            else "live event loop saw key events"
        ),
    )
    if require_input:
        print(
            "USB keyboard shell test: type "
            f"`echo {USB_KEYBOARD_SHELL_MARKER}` then Enter on the Fruit Jam USB keyboard.",
            flush=True,
        )
    yield pass_if(
        shell,
        "usb keyboard shell",
        f"fruitjam-usbhost kbd-auto-shell {seconds}",
        lambda r: (
            r.rc == 0
            and clean_output(r)
            and "USB keyboard shell" in r.output
            and "usbkbd$" in r.output
            and (not require_input or USB_KEYBOARD_SHELL_MARKER in r.output)
        ),
        timeout=seconds + 25,
        detail=(
            "keyboard shell loop completed"
            if not require_input
            else f"keyboard shell saw {USB_KEYBOARD_SHELL_MARKER}"
        ),
    )


def strip_usb_keyboard_noise(output: str) -> str:
    lines = []
    for line in output.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("usbhost keyboard "):
            continue
        if stripped.startswith("usbhost keyboard target "):
            continue
        lines.append(stripped)
    return "\n".join(lines).strip()


def airlift_tests(shell: CdcShell, args: argparse.Namespace) -> Iterable[TestResult]:
    shell.run("fruitjam-services stop; killall airliftctl 2>/dev/null || true", timeout=10)
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
        yield skip("airlift inbound http", "requires WiFi join")
        yield skip("airlift inbound shell", "requires WiFi join")
        yield skip("airlift inbound ftp", "requires WiFi join")
        return

    join_cmd = f"airliftctl join {shell_quote(ssid)} {shell_quote(password)}"
    join_result = shell.run(join_cmd, timeout=45)
    join_ok = not join_result.timed_out and contains("ip ")(join_result)
    if join_ok:
        yield TestResult("airlift join", "PASS", first_line(join_result.output), join_result.output)
    else:
        detail = f"timeout after 45s" if join_result.timed_out else f"rc={join_result.rc} {first_line(join_result.output)}"
        yield TestResult("airlift join", "FAIL", detail, join_result.output)
        yield skip("airlift tcp-get", "requires WiFi join")
        yield skip("airlift inbound shell", "requires WiFi join")
        yield skip("airlift inbound ftp", "requires WiFi join")
        return

    ip_match = re.search(r"\bip\s+(\d+\.\d+\.\d+\.\d+)\b", join_result.output)
    airlift_ip = ip_match.group(1) if ip_match else ""
    yield pass_if(
        shell,
        "airlift tcp-get",
        "airliftctl tcp-get example.com /",
        lambda r: r.rc == 0 and clean_output(r) and ("HTTP/" in r.output or "Example Domain" in r.output),
        timeout=45,
    )
    if airlift_ip:
        yield from airlift_inbound_tests(shell, airlift_ip)
    else:
        yield skip("airlift inbound http", "could not parse AirLift IP")
        yield skip("airlift inbound shell", "could not parse AirLift IP")
        yield skip("airlift inbound ftp", "could not parse AirLift IP")


def airlift_inbound_tests(shell: CdcShell, ip_addr: str) -> Iterable[TestResult]:
    start_cmd = (
        "fruitjam-services stop; "
        "airliftctl serve-inbound >/tmp/airlift-inbound.log 2>&1 & "
        "echo airlift-inbound:$!; sleep 2; cat /tmp/airlift-inbound.log"
    )
    start = shell.run(start_cmd, timeout=20)
    listening = (
        "shell listening on port 23" in start.output
        and "http listening on port 80" in start.output
        and "ftp listening on port 21" in start.output
    )
    if start.timed_out or start.rc != 0 or not listening:
        detail = "timeout starting bridge" if start.timed_out else f"rc={start.rc} {first_line(start.output)}"
        yield TestResult("airlift inbound http", "FAIL", detail, start.output)
        yield TestResult("airlift inbound shell", "FAIL", detail, start.output)
        yield skip("airlift inbound ftp", "requires inbound daemon")
        return

    try:
        http_root = run_inbound_http_probe(ip_addr, "/")
        http_status = run_inbound_http_probe(
            ip_addr, "/cgi-bin/fruitjam.cgi?action=status"
        )
        http_berry_list = run_inbound_http_probe(
            ip_addr, "/cgi-bin/fruitjam.cgi?action=berry-list"
        )
        http_berry_run = run_inbound_http_probe(
            ip_addr, "/cgi-bin/fruitjam.cgi?action=berry-run&script=00-hello.be"
        )
        http_output = (
            http_root
            + "\n--- status ---\n" + http_status
            + "\n--- berry-list ---\n" + http_berry_list
            + "\n--- berry-run ---\n" + http_berry_run
        )
    except OSError as exc:
        yield TestResult("airlift inbound http", "FAIL", str(exc), start.output)
    else:
        if (
            "HTTP/1.0 200 OK" in http_root
            and ("<h1>Fruit Jam</h1>" in http_root or "<title>Fruit Jam</title>" in http_root)
            and "HTTP/1.0 200 OK" in http_status
            and '"ok":true' in http_status
            and "HTTP/1.0 200 OK" in http_berry_list
            and '"run-all.be"' in http_berry_list
            and "HTTP/1.0 200 OK" in http_berry_run
            and '"ok":true' in http_berry_run
            and "00-hello.be: ok" in http_berry_run
        ):
            yield TestResult("airlift inbound http", "PASS", f"{ip_addr}:80 root + status + Berry run", http_output)
        else:
            yield TestResult("airlift inbound http", "FAIL", "HTTP root/status/Berry proof missing", http_output)

    try:
        shell_output = run_inbound_shell_probe(ip_addr)
    except OSError as exc:
        yield TestResult("airlift inbound shell", "FAIL", str(exc), start.output)
    else:
        if "Fruit Jam telnet shell" in shell_output and "inbound-smoke" in shell_output:
            yield TestResult("airlift inbound shell", "PASS", f"{ip_addr}:23 shell echo", shell_output)
        else:
            yield TestResult("airlift inbound shell", "FAIL", "shell proof text missing", shell_output)

    try:
        ftp_output = run_inbound_ftp_probe(ip_addr)
    except OSError as exc:
        yield TestResult("airlift inbound ftp", "FAIL", str(exc), "")
    else:
        if (
            "220 Fruit Jam FTP ready" in ftp_output
            and "230 Logged in" in ftp_output
            and "EPSV STOR 150 226" in ftp_output
            and "PASV STOR 150 226" in ftp_output
            and "EPSV RETR match: True" in ftp_output
            and "PASV RETR match: True" in ftp_output
            and "SECOND banner 220" in ftp_output
            and "SIZE " in ftp_output
            and INBOUND_UPLOAD_NAME in ftp_output
        ):
            yield TestResult("airlift inbound ftp", "PASS", f"{ip_addr}:21 passive EPSV/PASV STOR/RETR/NLST", ftp_output)
        else:
            yield TestResult("airlift inbound ftp", "FAIL", "FTP transfer proof missing", ftp_output)


def run_inbound_http_probe(ip_addr: str, target: str) -> str:
    request = (
        f"GET {target} HTTP/1.0\r\n"
        f"Host: {ip_addr}\r\n"
        "Connection: close\r\n\r\n"
    ).encode("ascii")
    data = bytearray()
    header_end = -1
    content_length: int | None = None
    deadline = time.monotonic() + 15.0

    with socket.create_connection((ip_addr, 80), timeout=10) as sock:
        sock.settimeout(1)
        sock.sendall(request)
        while time.monotonic() < deadline:
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                break
            data.extend(chunk)
            if header_end < 0:
                pos = data.find(b"\r\n\r\n")
                if pos >= 0:
                    header_end = pos + 4
                    for line in data[:pos].splitlines():
                        if line.lower().startswith(b"content-length:"):
                            try:
                                content_length = int(line.split(b":", 1)[1].strip())
                            except ValueError:
                                content_length = None
            if content_length is not None and header_end >= 0:
                if len(data) - header_end >= content_length:
                    break
    return bytes(data).decode("utf-8", "replace")


def run_inbound_shell_probe(ip_addr: str) -> str:
    return run_inbound_shell_script(ip_addr, b"echo inbound-smoke\nexit\n")


def run_inbound_shell_script(ip_addr: str, script: bytes) -> str:
    chunks: list[bytes] = []

    with socket.create_connection((ip_addr, 23), timeout=10) as sock:
        sock.settimeout(2)
        read_socket_chunks(sock, chunks, 3.0)
        sock.sendall(script)
        read_socket_chunks(sock, chunks, 5.0)

    return b"".join(chunks).decode("utf-8", "replace")


def run_inbound_ftp_probe(ip_addr: str) -> str:
    payload = INBOUND_UPLOAD_TEXT.encode("utf-8")
    transcript = bytearray()

    with socket.create_connection((ip_addr, 21), timeout=10) as sock:
        sock.settimeout(2)
        transcript += ftp_read_response(sock)
        for command in (b"USER anonymous\r\n", b"PASS fruitjam@\r\n", b"SYST\r\n", b"PWD\r\n", b"TYPE I\r\n"):
            sock.sendall(command)
            transcript += ftp_read_response(sock)

        for mode in ("EPSV", "PASV"):
            stor_status = ftp_passive_upload(sock, ip_addr, mode, INBOUND_UPLOAD_NAME, payload)
            transcript += f"{mode} STOR {stor_status}\r\n".encode("ascii")
            names = ftp_passive_download(
                sock, ip_addr, mode, f"NLST {INBOUND_UPLOAD_NAME}", text=True
            )
            transcript += f"{mode} NLST {names.strip()}\r\n".encode("utf-8")
            readback = ftp_passive_download(
                sock, ip_addr, mode, f"RETR {INBOUND_UPLOAD_NAME}"
            )
            transcript += f"{mode} RETR match: {readback == payload}\r\n".encode("ascii")
            sock.sendall(f"SIZE {INBOUND_UPLOAD_NAME}\r\n".encode("ascii"))
            transcript += f"{mode} SIZE ".encode("ascii") + ftp_read_response(sock)
            sock.sendall(f"DELE {INBOUND_UPLOAD_NAME}\r\n".encode("ascii"))
            transcript += f"{mode} ".encode("ascii") + ftp_read_response(sock)
        transcript += ftp_second_control_handoff(sock, ip_addr)
    return bytes(transcript).decode("utf-8", "replace")


def ftp_passive_open(control: socket.socket, ip_addr: str, mode: str) -> socket.socket:
    if mode == "EPSV":
        control.sendall(b"EPSV\r\n")
        reply = ftp_read_response(control)
        match = re.search(rb"\(\|\|\|(\d+)\|\)", reply)
        if not match:
            raise OSError(f"EPSV failed: {reply!r}")
        host = ip_addr
        port = int(match.group(1))
    elif mode == "PASV":
        control.sendall(b"PASV\r\n")
        reply = ftp_read_response(control)
        match = re.search(rb"\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)", reply)
        if not match:
            raise OSError(f"PASV failed: {reply!r}")
        nums = [int(group) for group in match.groups()]
        host = ".".join(str(part) for part in nums[:4])
        port = nums[4] * 256 + nums[5]
    else:
        raise ValueError(f"unknown passive FTP mode: {mode}")

    data = socket.create_connection((host, port), timeout=10)
    data.settimeout(6)
    return data


def ftp_passive_upload(
    control: socket.socket, ip_addr: str, mode: str, name: str, payload: bytes
) -> str:
    data = ftp_passive_open(control, ip_addr, mode)

    control.sendall(f"STOR {name}\r\n".encode("ascii"))
    start = ftp_read_response(control)
    if first_ftp_code(start) != "150":
        data.close()
        return f"{first_ftp_code(start)} 000"
    data.sendall(payload)
    try:
        data.shutdown(socket.SHUT_WR)
    except OSError:
        pass
    data.close()
    done = ftp_read_response(control, timeout=8.0)
    return f"{first_ftp_code(start)} {first_ftp_code(done)}"


def ftp_passive_download(
    control: socket.socket, ip_addr: str, mode: str, command: str, text: bool = False
) -> bytes | str:
    data = ftp_passive_open(control, ip_addr, mode)
    chunks: list[bytes] = []

    control.sendall(f"{command}\r\n".encode("ascii"))
    start = ftp_read_response(control)
    if first_ftp_code(start) != "150":
        data.close()
        raise OSError(f"{mode} {command} failed to start: {start!r}")
    read_socket_chunks(data, chunks, 6.0)
    data.close()
    done = ftp_read_response(control, timeout=8.0)
    if first_ftp_code(done) != "226":
        raise OSError(f"{mode} {command} failed to finish: {done!r}")
    payload = b"".join(chunks)
    return payload.decode("utf-8", "replace") if text else payload


def ftp_second_control_handoff(control: socket.socket, ip_addr: str) -> bytes:
    transcript = bytearray()

    with socket.create_connection((ip_addr, 21), timeout=10) as second:
        second.settimeout(6)
        banner = ftp_read_response(second)
        transcript += f"SECOND banner {first_ftp_code(banner)}\r\n".encode("ascii")
        if first_ftp_code(banner) != "220":
            raise OSError(f"second FTP control did not receive banner: {banner!r}")
        second.sendall(b"USER anonymous\r\n")
        transcript += b"SECOND " + ftp_read_response(second)
        second.sendall(b"PASS fruitjam-second@\r\n")
        transcript += b"SECOND " + ftp_read_response(second)
        second.sendall(b"QUIT\r\n")
        transcript += b"SECOND " + ftp_read_response(second)

    transcript += b"FIRST " + ftp_read_response(control, timeout=2.0)
    return bytes(transcript)


def ftp_active_listener(control: socket.socket) -> socket.socket:
    local_ip = control.getsockname()[0]
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((local_ip, 0))
    listener.listen(1)
    listener.settimeout(10)
    host, port = listener.getsockname()
    octets = host.split(".")
    if len(octets) != 4:
        listener.close()
        raise OSError(f"active FTP needs IPv4, got {host}")
    control.sendall(
        f"PORT {','.join(octets)},{port // 256},{port % 256}\r\n".encode("ascii")
    )
    reply = ftp_read_response(control)
    if first_ftp_code(reply) != "200":
        listener.close()
        raise OSError(f"PORT failed: {reply!r}")
    return listener


def ftp_active_accept(listener: socket.socket) -> socket.socket:
    data, _ = listener.accept()
    listener.close()
    data.settimeout(6)
    return data


def ftp_active_upload(control: socket.socket, name: str, payload: bytes) -> str:
    listener = ftp_active_listener(control)
    control.sendall(f"STOR {name}\r\n".encode("ascii"))
    start = ftp_read_response(control)
    if first_ftp_code(start) != "150":
        listener.close()
        return f"{first_ftp_code(start)} 000"
    data = ftp_active_accept(listener)
    data.sendall(payload)
    data.close()
    done = ftp_read_response(control, timeout=8.0)
    return f"{first_ftp_code(start)} {first_ftp_code(done)}"


def ftp_active_download(control: socket.socket, command: str, text: bool = False) -> bytes | str:
    listener = ftp_active_listener(control)
    chunks: list[bytes] = []

    control.sendall(f"{command}\r\n".encode("ascii"))
    start = ftp_read_response(control)
    if first_ftp_code(start) != "150":
        listener.close()
        raise OSError(f"{command} failed to start: {start!r}")
    data = ftp_active_accept(listener)
    read_socket_chunks(data, chunks, 6.0)
    data.close()
    done = ftp_read_response(control, timeout=8.0)
    if first_ftp_code(done) != "226":
        raise OSError(f"{command} failed to finish: {done!r}")
    payload = b"".join(chunks)
    return payload.decode("utf-8", "replace") if text else payload


def first_ftp_code(reply: bytes) -> str:
    text = reply.decode("utf-8", "replace").lstrip()
    return text[:3] if len(text) >= 3 and text[:3].isdigit() else "000"


def ftp_read_response(sock: socket.socket, timeout: float = 8.0) -> bytes:
    end = time.monotonic() + timeout
    chunks: list[bytes] = []

    while time.monotonic() < end:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            continue
        if not data:
            break
        chunks.append(data)
        text = b"".join(chunks)
        for line in text.splitlines():
            if re.match(rb"^\d{3} ", line):
                return text
    return b"".join(chunks)


def ftp_read_available(sock: socket.socket, timeout: float = 2.0) -> bytes:
    end = time.monotonic() + timeout
    chunks: list[bytes] = []

    while time.monotonic() < end:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            break
        if not data:
            break
        chunks.append(data)
        if data.endswith(b"\r\n"):
            break
    return b"".join(chunks)


def run_inbound_tftp_probe(ip_addr: str) -> str:
    upload = INBOUND_UPLOAD_TEXT.encode("utf-8")
    wrq = b"\x00\x02" + INBOUND_UPLOAD_NAME.encode("ascii") + b"\x00octet\x00"
    data_pkt = b"\x00\x03\x00\x01" + upload
    rrq = b"\x00\x01" + INBOUND_UPLOAD_NAME.encode("ascii") + b"\x00octet\x00"

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(6)
        sock.sendto(wrq, (ip_addr, 69))
        ack0, ack0_addr = sock.recvfrom(1024)
        if ack0[:4] != b"\x00\x04\x00\x00":
            return ack0.decode("utf-8", "replace")
        sock.sendto(data_pkt, ack0_addr)
        ack1, _ = sock.recvfrom(1024)
        if ack1[:4] != b"\x00\x04\x00\x01":
            return ack1.decode("utf-8", "replace")
        sock.sendto(rrq, (ip_addr, 69))
        data, _ = sock.recvfrom(1024)

    if len(data) >= 4 and data[1] == 3:
        return data[4:].decode("utf-8", "replace")
    return data.decode("utf-8", "replace")


def read_socket_chunks(sock: socket.socket, chunks: list[bytes], seconds: float) -> None:
    deadline = time.monotonic() + seconds

    while time.monotonic() < deadline:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            continue
        if not data:
            return
        chunks.append(data)
        if b"fj$ " in b"".join(chunks):
            return


def tool_inventory_command() -> str:
    tools = [
        "airliftctl",
        "berry",
        "fruitjamctl",
        "fruitjam-i2c",
        "fruitjam-rtttl",
        "fruitjam-wavplay",
        "fruitjam-dvi",
        "fruitjam-usbhost",
        "fruitjam-hidkeys",
        "fruitjam-services",
        "fruitjam-buttons",
        "berry-run",
        "mosquitto_pub",
        "mosquitto_sub",
        "fruitjam-mem",
        "free",
        "fruitjam-ps",
        "ps",
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
    parser.add_argument(
        "--usb-keyboard",
        action="store_true",
        help="run USB host boot-keyboard smoke tests against the plugged-in HID device",
    )
    parser.add_argument(
        "--usb-keyboard-seconds",
        type=int,
        default=int(os.environ.get("FJ_USB_KEYBOARD_SECONDS", "8")),
        help="seconds for live USB keyboard text/events/shell loops",
    )
    parser.add_argument(
        "--usb-keyboard-require-input",
        action="store_true",
        help="fail live USB keyboard loops unless typed text, key events, and shell input are captured",
    )
    parser.add_argument("--skip-display", action="store_true", help="skip the bounded DVI dashboard render")
    parser.add_argument("--skip-airlift", action="store_true", help="skip AirLift and WiFi tests")
    parser.add_argument("--skip-services", action="store_true", help="skip loopback network service tests")
    parser.add_argument(
        "--no-phase-reboot",
        action="store_true",
        help="kept for compatibility; the suite now leaves AirLift inbound running by default",
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

        if args.skip_services:
            results.append(skip("network services", "disabled by --skip-services"))
        else:
            results.extend(service_tests(shell))

        if args.usb_keyboard:
            results.extend(usbhost_keyboard_tests(shell, args))
        else:
            results.append(skip("usb keyboard", "use --usb-keyboard with a boot HID keyboard attached"))

        if args.skip_airlift:
            results.append(skip("airlift", "disabled by --skip-airlift"))
        else:
            results.extend(airlift_tests(shell, args))
        return print_report(results, args.verbose)
    finally:
        shell.close()


if __name__ == "__main__":
    raise SystemExit(main())
