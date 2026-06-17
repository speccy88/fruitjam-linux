#!/usr/bin/env python3
"""Run Fruit Jam AirLift MQTT publish/subscribe smoke tests."""

from __future__ import annotations

import argparse
import os
import re
import shlex
import socket
import struct
import sys
import threading
import time
from dataclasses import dataclass
from typing import Protocol


CONTROL_NOISE = b"\x10\x01\x01"
DEFAULT_PORT = os.environ.get("FJ_CDC_PORT", "/dev/tty.usbmodem1101")
DEFAULT_BAUD = int(os.environ.get("FJ_CDC_BAUD", "115200"))


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


def scrub(text: str, secret: str | None) -> str:
    if secret:
        text = text.replace(secret, "***")
    text = re.sub(r"(-P\s+)'[^']*'", r"\1'***'", text)
    text = re.sub(r'(-P\s+)"[^"]*"', r'\1"***"', text)
    return text


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
    def __init__(self, port: str, baud: int, verbose: bool, secret: str | None):
        try:
            import serial
        except ImportError as exc:  # pragma: no cover - host dependency guard
            raise SystemExit("pyserial is required for CDC mode: python3 -m pip install pyserial") from exc

        self.verbose = verbose
        self.secret = secret
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
        ident = f"FJMQTT_{self.counter:04d}"
        begin = f"__{ident}_BEGIN__"
        end = f"__{ident}_END__"
        wrapped = f"echo {begin}; {command}; rc=$?; echo {end}:$rc"

        if self.verbose:
            print(f"$ {scrub(command, self.secret)}", file=sys.stderr)

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

        output = extract_payload(decode(bytes(raw)), begin, end)
        rc = int(match.group(1)) if match else None
        return CommandResult(command, scrub(output, self.secret), rc, timed_out)


class TelnetShell:
    def __init__(self, host: str, port: int, verbose: bool, secret: str | None):
        self.host = host
        self.port = port
        self.verbose = verbose
        self.secret = secret
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
        ident = f"FJMQTT_{self.counter:04d}"
        begin = f"__{ident}_BEGIN__"
        end = f"__{ident}_END__"
        wrapped = f"echo {begin}; {command}; rc=$?; echo {end}:$rc"

        if self.verbose:
            print(f"$ {scrub(command, self.secret)}", file=sys.stderr)

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
        except OSError as exc:
            return CommandResult(command, f"telnet error: {exc}", None, False)

        output = extract_payload(decode(bytes(raw)), begin, end)
        rc = int(match.group(1)) if match else None
        return CommandResult(command, scrub(output, self.secret), rc, match is None)


class FakeShell:
    def __init__(self, target_sub_message: str):
        self.target_sub_message = target_sub_message

    def run(self, command: str, timeout: float = 12.0) -> CommandResult:
        del timeout
        if command.startswith("mosquitto_pub --airlift "):
            return CommandResult(command, "published", 0, False)
        if command.startswith("mosquitto_sub --airlift "):
            return CommandResult(command, self.target_sub_message, 0, False)
        return CommandResult(command, "unexpected command", 127, False)

    def close(self) -> None:
        return None


def mqtt_remaining_length(length: int) -> bytes:
    out = bytearray()
    while True:
        encoded = length % 128
        length //= 128
        if length:
            encoded |= 0x80
        out.append(encoded)
        if not length:
            return bytes(out)


def mqtt_string(value: str) -> bytes:
    data = value.encode("utf-8")
    return struct.pack("!H", len(data)) + data


def mqtt_packet(packet_type: int, payload: bytes) -> bytes:
    return bytes([packet_type]) + mqtt_remaining_length(len(payload)) + payload


def mqtt_read_packet(sock: socket.socket, timeout: float) -> tuple[int, bytes]:
    sock.settimeout(timeout)
    header = sock.recv(1)
    if not header:
        raise TimeoutError("MQTT socket closed")
    multiplier = 1
    remaining = 0
    while True:
        b = sock.recv(1)
        if not b:
            raise TimeoutError("MQTT remaining length closed")
        encoded = b[0]
        remaining += (encoded & 127) * multiplier
        if not encoded & 128:
            break
        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise ValueError("MQTT remaining length too large")
    payload = bytearray()
    while len(payload) < remaining:
        chunk = sock.recv(remaining - len(payload))
        if not chunk:
            raise TimeoutError("MQTT payload closed")
        payload.extend(chunk)
    return header[0], bytes(payload)


def mqtt_connect(sock: socket.socket, client_id: str, username: str | None, password: str | None) -> None:
    flags = 0x02
    if username:
        flags |= 0x80
    if password is not None:
        flags |= 0x40
    body = mqtt_string("MQTT") + bytes([4, flags, 0, 30]) + mqtt_string(client_id)
    if username:
        body += mqtt_string(username)
    if password is not None:
        body += mqtt_string(password)
    sock.sendall(mqtt_packet(0x10, body))
    header, payload = mqtt_read_packet(sock, 5)
    if header != 0x20 or len(payload) < 2 or payload[1] != 0:
        raise RuntimeError(f"MQTT connect failed header=0x{header:02x} payload={payload!r}")


def mqtt_publish(host: str, port: int, topic: str, message: str,
                 client_id: str, username: str | None, password: str | None,
                 timeout: float) -> None:
    with socket.create_connection((host, port), timeout=timeout) as sock:
        mqtt_connect(sock, client_id, username, password)
        sock.sendall(mqtt_packet(0x30, mqtt_string(topic) + message.encode("utf-8")))
        sock.sendall(b"\xe0\x00")


def mqtt_subscribe_once(host: str, port: int, topic: str, client_id: str,
                        username: str | None, password: str | None,
                        timeout: float) -> str:
    deadline = time.monotonic() + timeout
    with socket.create_connection((host, port), timeout=timeout) as sock:
        mqtt_connect(sock, client_id, username, password)
        packet_id = 1
        body = struct.pack("!H", packet_id) + mqtt_string(topic) + b"\x00"
        sock.sendall(mqtt_packet(0x82, body))
        header, payload = mqtt_read_packet(sock, max(0.1, deadline - time.monotonic()))
        if header != 0x90 or len(payload) < 3 or payload[2] == 0x80:
            raise RuntimeError(f"MQTT subscribe failed header=0x{header:02x} payload={payload!r}")
        while time.monotonic() < deadline:
            header, payload = mqtt_read_packet(sock, max(0.1, deadline - time.monotonic()))
            if header & 0xf0 != 0x30 or len(payload) < 2:
                continue
            topic_len = struct.unpack("!H", payload[:2])[0]
            msg_start = 2 + topic_len
            if msg_start <= len(payload):
                return payload[msg_start:].decode("utf-8", "replace")
    raise TimeoutError("MQTT publish not received")


class FakeMqtt:
    def __init__(self, target_message: str):
        self.target_message = target_message
        self.host_published = ""

    def subscribe_once(self, **kwargs: object) -> str:
        del kwargs
        return self.target_message

    def publish(self, **kwargs: object) -> None:
        self.host_published = str(kwargs.get("message", ""))


def shell_quote(value: object) -> str:
    return shlex.quote(str(value))


def target_pub_command(args: argparse.Namespace, topic: str, message: str) -> str:
    parts = [
        "mosquitto_pub", "--airlift",
        "-h", args.host,
        "-p", str(args.mqtt_port),
        "-i", args.target_pub_client,
    ]
    if args.username:
        parts += ["-u", args.username]
    if args.password is not None:
        parts += ["-P", args.password]
    parts += ["-t", topic, "-m", message]
    return " ".join(shell_quote(p) for p in parts)


def target_sub_command(args: argparse.Namespace, topic: str) -> str:
    parts = [
        "mosquitto_sub", "--airlift",
        "-h", args.host,
        "-p", str(args.mqtt_port),
        "-i", args.target_sub_client,
    ]
    if args.username:
        parts += ["-u", args.username]
    if args.password is not None:
        parts += ["-P", args.password]
    parts += ["-t", topic, "-C", "1", "-W", str(args.wait), "-v"]
    return " ".join(shell_quote(p) for p in parts)


def first_line(text: str) -> str:
    for line in text.splitlines():
        if line.strip():
            return line.strip()
    return "(no output)"


def run_smoke(shell: Shell, args: argparse.Namespace, mqtt: FakeMqtt | None = None) -> list[TestResult]:
    topic = args.topic
    stamp = int(time.time())
    target_message = f"fruitjam-target-pub-{stamp}"
    host_message = f"fruitjam-host-pub-{stamp}"
    results: list[TestResult] = []

    if mqtt is None:
        host_sub_result: dict[str, str | Exception] = {}

        def host_subscribe() -> None:
            try:
                host_sub_result["message"] = mqtt_subscribe_once(
                    args.host, args.mqtt_port, topic, args.host_sub_client,
                    args.username, args.password, args.wait + 10,
                )
            except Exception as exc:  # pragma: no cover - live network path
                host_sub_result["error"] = exc

        sub_thread = threading.Thread(target=host_subscribe, daemon=True)
        sub_thread.start()
        time.sleep(1.0)
    else:
        mqtt.target_message = target_message
        host_sub_result = {}
        sub_thread = None

    pub_result = shell.run(target_pub_command(args, topic, target_message), timeout=args.wait + 20)
    if mqtt is not None:
        host_sub_result["message"] = mqtt.subscribe_once()
    elif sub_thread is not None:
        sub_thread.join(args.wait + 12)

    received = host_sub_result.get("message")
    if pub_result.timed_out:
        results.append(TestResult("target mqtt publish", "FAIL", f"timeout after {args.wait + 20}s", pub_result.output))
    elif pub_result.rc != 0:
        results.append(TestResult("target mqtt publish", "FAIL", f"rc={pub_result.rc} {first_line(pub_result.output)}", pub_result.output))
    elif received == target_message:
        results.append(TestResult("target mqtt publish", "PASS", f"host received {topic}", pub_result.output))
    else:
        err = host_sub_result.get("error")
        detail = f"host subscribe failed: {err}" if err else f"host received {received!r}"
        results.append(TestResult("target mqtt publish", "FAIL", detail, pub_result.output))

    sub_result_box: dict[str, CommandResult] = {}

    def target_subscribe() -> None:
        sub_result_box["result"] = shell.run(target_sub_command(args, topic), timeout=args.wait + 20)

    if isinstance(shell, FakeShell):
        shell.target_sub_message = f"{topic} {host_message}"
    target_thread = threading.Thread(target=target_subscribe, daemon=True)
    target_thread.start()
    time.sleep(1.5)
    if mqtt is None:
        try:
            mqtt_publish(args.host, args.mqtt_port, topic, host_message,
                         args.host_pub_client, args.username, args.password,
                         args.wait)
        except Exception as exc:
            sub_result_box["host_error"] = CommandResult("host mqtt publish", str(exc), None, False)
    else:
        mqtt.publish(message=host_message)
    target_thread.join(args.wait + 22)
    sub_result = sub_result_box.get("result")
    host_error = sub_result_box.get("host_error")

    if host_error is not None:
        results.append(TestResult("target mqtt subscribe", "FAIL", f"host publish failed: {host_error.output}", host_error.output))
    elif sub_result is None:
        results.append(TestResult("target mqtt subscribe", "FAIL", "target subscribe thread did not finish"))
    elif sub_result.timed_out:
        results.append(TestResult("target mqtt subscribe", "FAIL", f"timeout after {args.wait + 20}s", sub_result.output))
    elif sub_result.rc != 0:
        results.append(TestResult("target mqtt subscribe", "FAIL", f"rc={sub_result.rc} {first_line(sub_result.output)}", sub_result.output))
    elif host_message in sub_result.output:
        results.append(TestResult("target mqtt subscribe", "PASS", f"target received {topic}", sub_result.output))
    else:
        results.append(TestResult("target mqtt subscribe", "FAIL", f"message missing: {first_line(sub_result.output)}", sub_result.output))

    return results


def print_report(results: list[TestResult], verbose: bool, secret: str | None) -> int:
    width = max(len(r.name) for r in results) if results else 0
    failed = 0
    for result in results:
        if result.status == "FAIL":
            failed += 1
        print(f"{result.status:4} {result.name:<{width}}  {scrub(result.detail, secret)}")
        if verbose and result.output:
            for line in scrub(result.output, secret).splitlines():
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
    parser.add_argument("--host", default=os.environ.get("FJ_MQTT_HOST"), help="MQTT broker host/IP")
    parser.add_argument("--mqtt-port", type=int, default=int(os.environ.get("FJ_MQTT_PORT", "1883")))
    parser.add_argument("--username", default=os.environ.get("FJ_MQTT_USERNAME"))
    parser.add_argument("--password", default=os.environ.get("FJ_MQTT_PASSWORD"))
    parser.add_argument("--topic", default=os.environ.get("FJ_MQTT_TOPIC", "charlie/test"))
    parser.add_argument("--wait", type=int, default=int(os.environ.get("FJ_MQTT_WAIT", "20")))
    parser.add_argument("--target-pub-client", default="fruitjam-target-pub")
    parser.add_argument("--target-sub-client", default="fruitjam-target-sub")
    parser.add_argument("--host-pub-client", default="fruitjam-host-pub")
    parser.add_argument("--host-sub-client", default="fruitjam-host-sub")
    parser.add_argument("--self-test", action="store_true", help="run command-flow checks without a board or broker")
    parser.add_argument("-v", "--verbose", action="store_true", help="show scrubbed command output")
    return parser.parse_args()


def make_shell(args: argparse.Namespace, target_sub_message: str) -> Shell:
    if args.self_test:
        return FakeShell(target_sub_message)
    if args.transport == "telnet" or (args.transport == "auto" and args.telnet_host):
        if not args.telnet_host:
            raise SystemExit("--telnet-host is required for telnet transport")
        return TelnetShell(args.telnet_host, args.telnet_port, args.verbose, args.password)
    return SerialShell(args.port, args.baud, args.verbose, args.password)


def main() -> int:
    args = parse_args()
    if not args.self_test and not args.host:
        raise SystemExit("--host or FJ_MQTT_HOST is required")
    if args.password is not None and not args.username:
        raise SystemExit("--password/FJ_MQTT_PASSWORD requires --username/FJ_MQTT_USERNAME")
    if args.self_test and not args.host:
        args.host = "broker.local"
    args.password = args.password if args.password is not None else None
    target_sub_message = f"{args.topic} fruitjam-host-pub-selftest"
    shell = make_shell(args, target_sub_message)
    try:
        mqtt = FakeMqtt("fruitjam-target-pub-selftest") if args.self_test else None
        results = run_smoke(shell, args, mqtt=mqtt)
        return print_report(results, args.verbose, args.password)
    finally:
        shell.close()


if __name__ == "__main__":
    raise SystemExit(main())
