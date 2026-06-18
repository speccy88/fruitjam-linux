#!/usr/bin/env python3
"""Recover a Fruit Jam RP2350 into BOOTSEL and flash the current UF2."""

from __future__ import annotations

import argparse
import glob
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
from urllib.parse import quote, urlsplit, urlunsplit
from urllib.request import Request, urlopen


DEFAULT_UF2 = Path("buildroot-output-docker-images/flash-image.uf2")
DEFAULT_PICOTOOL = os.environ.get("PICOTOOL", "picotool")
DEFAULT_HTTP_HOST = os.environ.get("FJ_HTTP_HOST", "")
DEFAULT_HTTP_PORT = int(os.environ.get("FJ_HTTP_PORT", "80"))
DEFAULT_TELNET_HOST = os.environ.get("FJ_TELNET_HOST", "")
DEFAULT_TELNET_PORT = int(os.environ.get("FJ_TELNET_PORT", "23"))
DEFAULT_UART_PORT = os.environ.get("FJ_UART_PORT", "")
DEFAULT_UART_BAUD = int(os.environ.get("FJ_UART_BAUD", "115200"))
DEFAULT_POST_TRIGGER_BOOTSEL_TIMEOUT = float(os.environ.get("FJ_POST_TRIGGER_BOOTSEL_TIMEOUT", "30"))
DEFAULT_MANUAL_BOOTSEL_TIMEOUT = float(os.environ.get("FJ_MANUAL_BOOTSEL_TIMEOUT", "120"))
DEFAULT_FLASH_TIMEOUT = float(os.environ.get("FJ_FLASH_TIMEOUT", "180"))
DEFAULT_BARK_URL = os.environ.get("FJ_BARK_URL", os.environ.get("BARK_URL", ""))
DEFAULT_INCLUDE_TTY_COUNTERPART = (
    os.environ.get("FJ_INCLUDE_TTY_COUNTERPART", "1")
    not in ("0", "false", "False", "no", "NO")
)
DEFAULT_AIRLIFT_DISCOVERY = (
    os.environ.get("FJ_AIRLIFT_DISCOVERY", "1")
    not in ("0", "false", "False", "no", "NO")
)
DEFAULT_UART_DISCOVERY = (
    os.environ.get("FJ_UART_DISCOVERY", "1")
    not in ("0", "false", "False", "no", "NO")
)


def log(message: str) -> None:
    print(message, flush=True)


def run_cmd(argv: list[str], timeout: float, verbose: bool) -> subprocess.CompletedProcess[str]:
    if verbose:
        log("+ " + " ".join(argv))
    return subprocess.run(
        argv,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
        check=False,
    )


def picotool_info(picotool: str, verbose: bool) -> bool:
    try:
        result = run_cmd([picotool, "info", "-a"], timeout=8.0, verbose=verbose)
    except (OSError, subprocess.TimeoutExpired) as exc:
        if verbose:
            log(f"picotool info failed: {exc}")
        return False
    if verbose and result.stdout.strip():
        log(result.stdout.rstrip())
    return result.returncode == 0


def wait_for_bootsel(picotool: str, timeout: float, verbose: bool) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if picotool_info(picotool, verbose=False):
            if verbose:
                picotool_info(picotool, verbose=True)
            return True
        time.sleep(0.5)
    return False


def picotool_force_bootsel(picotool: str, verbose: bool) -> bool:
    try:
        result = run_cmd([picotool, "reboot", "-u", "-f"], timeout=12.0, verbose=verbose)
    except (OSError, subprocess.TimeoutExpired) as exc:
        if verbose:
            log(f"picotool forced BOOTSEL reboot failed: {exc}")
        return False
    if result.stdout.strip() and verbose:
        log(result.stdout.rstrip())
    if result.returncode != 0:
        if verbose:
            detail = result.stdout.strip() or f"exit {result.returncode}"
            log(f"picotool forced BOOTSEL reboot failed: {detail}")
        return False
    log("sent picotool forced USB BOOTSEL reboot")
    return True


def post_trigger_bootsel_timeout(args: argparse.Namespace) -> float:
    return max(args.bootsel_timeout, args.post_trigger_bootsel_timeout)


def auto_cdc_ports(include_tty_counterparts: bool = False) -> list[str]:
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if include_tty_counterparts or sys.platform != "darwin":
        ports += sorted(glob.glob("/dev/tty.usbmodem*"))
    return unique_ports(ports)


def auto_uart_ports(verbose: bool) -> list[str]:
    patterns = (
        "/dev/cu.usbserial*",
        "/dev/cu.SLAB_USBtoUART*",
        "/dev/cu.wchusbserial*",
        "/dev/ttyUSB*",
        "/dev/ttyACM*",
    )
    ports: list[str] = []
    for pattern in patterns:
        ports += sorted(glob.glob(pattern))
    ports = [port for port in unique_ports(ports) if "usbmodem" not in port]
    if verbose and ports:
        log("discovered UART ports: " + ", ".join(ports))
    return ports


def cdc_counterpart(port: str) -> str | None:
    if port.startswith("/dev/cu."):
        return "/dev/tty." + port[len("/dev/cu."):]
    if port.startswith("/dev/tty."):
        return "/dev/cu." + port[len("/dev/tty."):]
    return None


def unique_ports(ports: list[str]) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for port in ports:
        if port and port not in seen:
            seen.add(port)
            result.append(port)
    return result


def auto_airlift_hosts(verbose: bool) -> list[str]:
    try:
        result = subprocess.run(
            ["arp", "-an"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=4.0,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        if verbose:
            log(f"AirLift ARP discovery failed: {exc}")
        return []

    hosts: list[str] = []
    esp32_prefixes = ("58:e6:c5:", "58:e6:c5 ", "58:e6:c5)")
    for line in result.stdout.splitlines():
        lower = line.lower()
        esp32_mac = any(prefix in lower for prefix in esp32_prefixes)
        if (
            "esp32c6-" not in lower and
            "fruitjam" not in lower and
            "airlift" not in lower and
            not esp32_mac
        ):
            continue
        match = re.search(r"\((\d+\.\d+\.\d+\.\d+)\)", line)
        if not match:
            continue
        host = match.group(1)
        hosts.append(host)
        if verbose:
            log(f"discovered AirLift host {host} from ARP: {line.strip()}")
    return unique_ports(hosts)


def recovery_hosts(args: argparse.Namespace) -> list[str]:
    hosts = [args.http_host, args.telnet_host]
    if args.airlift_discovery:
        hosts += auto_airlift_hosts(args.verbose)
    return unique_ports(hosts)


def selected_cdc_ports(port: str | None, include_tty_counterparts: bool = False) -> list[str]:
    if port:
        ports = [port]
        counterpart = cdc_counterpart(port)
        if include_tty_counterparts and counterpart and os.path.exists(counterpart):
            ports.append(counterpart)
        return unique_ports(ports)
    env_port = os.environ.get("FJ_CDC_PORT")
    if env_port:
        return selected_cdc_ports(env_port, include_tty_counterparts)
    return auto_cdc_ports(include_tty_counterparts)


def selected_uart_ports(port: str | None, discover: bool, verbose: bool) -> list[str]:
    if port:
        return unique_ports([port])
    if DEFAULT_UART_PORT:
        return unique_ports([DEFAULT_UART_PORT])
    if discover:
        return auto_uart_ports(verbose)
    return []


def close_own_fds_for_path(path: str, verbose: bool) -> None:
    fd_dir = "/proc/self/fd" if os.path.isdir("/proc/self/fd") else "/dev/fd"
    targets = {path, os.path.realpath(path)}

    try:
        fd_names = os.listdir(fd_dir)
    except OSError:
        return

    for name in fd_names:
        try:
            fd = int(name)
        except ValueError:
            continue
        if fd <= 2:
            continue
        fd_path = os.path.join(fd_dir, name)
        candidates: list[str] = []
        try:
            candidates.append(os.readlink(fd_path))
        except OSError:
            pass
        if sys.platform == "darwin":
            try:
                import fcntl

                raw = fcntl.fcntl(fd, 50, b"\0" * 1024)
                resolved = raw.split(b"\0", 1)[0].decode("utf-8", "replace")
                if resolved:
                    candidates.append(resolved)
            except OSError:
                pass
        try:
            candidates.append(os.path.realpath(fd_path))
        except OSError:
            pass
        if not any(candidate in targets or os.path.realpath(candidate) in targets
                   for candidate in candidates):
            continue
        try:
            os.close(fd)
            if verbose:
                log(f"closed lingering CDC fd {fd} for {path}")
        except OSError:
            pass


def run_serial_child(label: str, code: str, argv: list[str], timeout: float, verbose: bool) -> bool:
    try:
        result = subprocess.run(
            [sys.executable, "-c", code, *argv],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout if timeout > 0 else None,
            check=False,
        )
    except subprocess.TimeoutExpired:
        if verbose:
            log(f"{label} timed out after {timeout:g}s")
        return False
    except OSError as exc:
        if verbose:
            log(f"{label} failed to start: {exc}")
        return False

    if result.stdout.strip() and verbose:
        log(result.stdout.rstrip())
    if result.returncode != 0:
        if verbose:
            detail = result.stdout.strip() or f"exit {result.returncode}"
            log(f"{label} failed: {detail}")
        return False
    return True


def cdc_shell_bootsel(port: str, baud: int, open_timeout: float, verbose: bool) -> bool:
    code = """
import sys
import time
import serial

ser = serial.Serial(
    sys.argv[1],
    int(sys.argv[2]),
    timeout=0.2,
    write_timeout=1,
    dsrdtr=False,
    rtscts=False,
)
try:
    ser.dtr = True
    ser.rts = False
    time.sleep(0.2)
    try:
        ser.reset_input_buffer()
    except Exception:
        pass
    ser.write(
        b"\\r\\nsync\\r\\n"
        b"bootsel 250\\r\\n"
        b"/usr/bin/fruitjamctl bootsel 250\\r\\n"
        b"fruitjamctl bootsel 250\\r\\n"
    )
    ser.flush()
    time.sleep(0.5)
finally:
    ser.close()
"""
    if run_serial_child(
        f"CDC shell BOOTSEL on {port}",
        code,
        [port, str(baud)],
        open_timeout,
        verbose,
    ):
        log(f"sent BOOTSEL commands over CDC {port}")
        return True
    close_own_fds_for_path(port, verbose)
    time.sleep(0.5)
    return False


def cdc_raw_shell_bootsel(port: str, open_timeout: float, verbose: bool) -> bool:
    code = """
import os
import select
import sys
import time

port = sys.argv[1]
flags = os.O_RDWR | os.O_NONBLOCK | getattr(os, "O_NOCTTY", 0)
payload = (
    b"\\r\\n"
    b"bootsel 250\\r\\n"
    b"/usr/bin/fruitjamctl bootsel 250\\r\\n"
    b"fruitjamctl bootsel 250\\r\\n"
)

try:
    fd = os.open(port, flags)
except OSError as exc:
    print(f"open failed: {exc}")
    raise SystemExit(1) from None

try:
    view = memoryview(payload)
    written = 0
    deadline = time.monotonic() + 1.5
    while written < len(payload):
        _, ready, _ = select.select([], [fd], [], 0.1)
        if not ready:
            if time.monotonic() >= deadline:
                raise TimeoutError("raw CDC write timed out")
            continue
        try:
            n = os.write(fd, view[written:])
        except BlockingIOError:
            if time.monotonic() >= deadline:
                raise TimeoutError("raw CDC write stayed busy")
            continue
        if n <= 0:
            raise OSError("raw CDC write made no progress")
        written += n
except (OSError, TimeoutError) as exc:
    print(f"write failed: {exc}")
    raise SystemExit(1) from None
finally:
    os.close(fd)
"""
    if run_serial_child(
        f"CDC raw shell BOOTSEL on {port}",
        code,
        [port],
        open_timeout,
        verbose,
    ):
        log(f"sent raw CDC BOOTSEL commands over {port}")
        return True
    close_own_fds_for_path(port, verbose)
    time.sleep(0.5)
    return False


def uart_shell_bootsel(port: str, baud: int, open_timeout: float, verbose: bool) -> bool:
    code = """
import sys
import time
import serial

ser = serial.Serial()
ser.port = sys.argv[1]
ser.baudrate = int(sys.argv[2])
ser.timeout = 0.2
ser.write_timeout = 1
ser.dsrdtr = False
ser.rtscts = False
ser.dtr = False
ser.rts = False
ser.open()
try:
    ser.dtr = False
    ser.rts = False
    time.sleep(0.2)
    try:
        ser.reset_input_buffer()
    except Exception:
        pass
    ser.write(b"\\r\\n")
    ser.flush()
    time.sleep(0.25)
    ser.write(
        b"bootsel 250\\r\\n"
        b"/usr/bin/fruitjamctl bootsel 250\\r\\n"
        b"fruitjamctl bootsel 250\\r\\n"
    )
    ser.flush()
    time.sleep(0.5)
finally:
    ser.close()
"""
    if run_serial_child(
        f"UART shell BOOTSEL on {port}",
        code,
        [port, str(baud)],
        open_timeout,
        verbose,
    ):
        log(f"sent BOOTSEL commands over UART {port}")
        return True
    close_own_fds_for_path(port, verbose)
    time.sleep(0.5)
    return False


def cdc_1200_touch_bootsel(port: str, open_timeout: float, verbose: bool) -> bool:
    code = """
import sys
import time
import serial

ser = serial.Serial(
    sys.argv[1],
    1200,
    timeout=0.2,
    write_timeout=1,
    dsrdtr=False,
    rtscts=False,
)
try:
    ser.dtr = False
    ser.rts = False
    time.sleep(0.25)
finally:
    ser.close()
"""
    if run_serial_child(
        f"CDC 1200-baud BOOTSEL touch on {port}",
        code,
        [port],
        open_timeout,
        verbose,
    ):
        log(f"sent 1200-baud DTR-low BOOTSEL touch on CDC {port}")
        return True
    close_own_fds_for_path(port, verbose)
    time.sleep(0.5)
    return False


def cdc_1200_native_touch_bootsel(port: str, open_timeout: float, verbose: bool) -> bool:
    code = """
import array
import fcntl
import os
import struct
import sys
import termios
import time

port = sys.argv[1]
flags = os.O_RDWR | os.O_NONBLOCK | getattr(os, "O_NOCTTY", 0)

try:
    fd = os.open(port, flags)
except OSError as exc:
    print(f"open failed: {exc}")
    raise SystemExit(1) from None

try:
    attrs = termios.tcgetattr(fd)
    attrs[4] = termios.B1200
    attrs[5] = termios.B1200
    attrs[2] |= getattr(termios, "HUPCL", 0)
    termios.tcsetattr(fd, termios.TCSANOW, attrs)

    clear_mask = getattr(termios, "TIOCM_DTR", 0) | getattr(termios, "TIOCM_RTS", 0)
    if clear_mask and hasattr(termios, "TIOCMBIC"):
        fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack("i", clear_mask))
    elif clear_mask and hasattr(termios, "TIOCMGET") and hasattr(termios, "TIOCMSET"):
        mask = array.array("i", [0])
        fcntl.ioctl(fd, termios.TIOCMGET, mask, True)
        mask[0] &= ~clear_mask
        fcntl.ioctl(fd, termios.TIOCMSET, mask)

    time.sleep(0.25)
finally:
    os.close(fd)
"""
    if run_serial_child(
        f"CDC native 1200-baud DTR-low BOOTSEL touch on {port}",
        code,
        [port],
        open_timeout,
        verbose,
    ):
        log(f"sent native 1200-baud DTR-low BOOTSEL touch on CDC {port}")
        return True
    close_own_fds_for_path(port, verbose)
    time.sleep(0.5)
    return False


def cdc_1200_stty_bootsel(port: str, timeout: float, verbose: bool) -> bool:
    stty_device_flag = "-f" if sys.platform == "darwin" else "-F"
    try:
        result = run_cmd(
            ["stty", stty_device_flag, port, "1200", "hupcl"],
            timeout=timeout,
            verbose=verbose,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        if verbose:
            log(f"CDC stty 1200-baud BOOTSEL touch failed on {port}: {exc}")
        return False
    if result.stdout.strip() and verbose:
        log(result.stdout.rstrip())
    if result.returncode != 0:
        if verbose:
            log(f"CDC stty 1200-baud BOOTSEL touch failed on {port}: exit {result.returncode}")
        return False
    log(f"sent stty 1200-baud BOOTSEL touch on CDC {port}")
    return True


def cdc_usb_control_bootsel(verbose: bool) -> bool:
    try:
        import usb.core
    except ImportError as exc:
        if verbose:
            log(f"CDC USB-control BOOTSEL unavailable: {exc}")
        return False

    devices = list(usb.core.find(find_all=True, idVendor=0x0525, idProduct=0xA4A7))
    if not devices:
        if verbose:
            log("CDC USB-control BOOTSEL: no Linux Gadget Serial device found")
        return False

    # CDC ACM SET_LINE_CODING: 1200 baud, 1 stop bit, no parity, 8 data bits.
    line_coding = struct.pack("<IBBB", 1200, 0, 0, 8)
    for dev in devices:
        try:
            dev.ctrl_transfer(0x21, 0x20, 0, 0, line_coding, timeout=1000)
            try:
                dev.ctrl_transfer(0x21, 0x22, 0, 0, None, timeout=1000)
            except Exception as exc:
                if verbose:
                    log(f"CDC USB-control DTR clear failed after 1200-baud touch: {exc}")
            log("sent CDC USB-control 1200-baud BOOTSEL touch")
            return True
        except Exception as exc:
            if verbose:
                log(f"CDC USB-control BOOTSEL failed on 0525:a4a7: {exc}")
    return False


def http_bootsel(host: str, port: int, verbose: bool) -> bool:
    request = (
        "GET /cgi-bin/fruitjam.cgi?action=bootsel HTTP/1.0\r\n"
        f"Host: {host}\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).encode("ascii")
    request_sent = False
    try:
        with socket.create_connection((host, port), timeout=5.0) as sock:
            sock.settimeout(0.4)
            sock.sendall(request)
            request_sent = True
            response = bytearray()
            deadline = time.monotonic() + 2.0
            while time.monotonic() < deadline:
                try:
                    data = sock.recv(4096)
                except socket.timeout:
                    continue
                except OSError as exc:
                    if request_sent:
                        if verbose:
                            log(f"HTTP BOOTSEL socket closed after request to {host}:{port}: {exc}")
                        log(f"sent direct HTTP BOOTSEL request to {host}:{port}; socket closed before reply")
                        return True
                    raise
                if not data:
                    break
                response.extend(data)
            if verbose and response:
                log(response.decode("utf-8", "replace").rstrip())
        if response:
            log(f"sent direct HTTP BOOTSEL request to {host}:{port}")
        else:
            log(f"sent direct HTTP BOOTSEL request to {host}:{port}; no HTTP reply before timeout")
        return True
    except OSError as exc:
        if verbose:
            log(f"HTTP BOOTSEL failed on {host}:{port}: {exc}")
        return False


def telnet_bootsel(host: str, port: int, verbose: bool) -> bool:
    try:
        with socket.create_connection((host, port), timeout=6.0) as sock:
            sock.settimeout(0.4)
            deadline = time.monotonic() + 2.0
            initial = bytearray()
            while time.monotonic() < deadline:
                try:
                    data = sock.recv(4096)
                except socket.timeout:
                    continue
                initial.extend(data)
                if not data or b"fj$" in data or b"#" in data:
                    break
            if verbose and initial:
                log(initial.decode("utf-8", "replace").rstrip())
            if b"busy" in initial:
                return False
            sock.sendall(b"\r\nbootsel 250\r\n/usr/bin/fruitjamctl bootsel 250\r\n")
            time.sleep(0.25)
        if initial:
            log(f"sent fruitjamctl bootsel over telnet {host}:{port}")
        else:
            log(f"sent fruitjamctl bootsel over telnet {host}:{port}; no prompt before command")
        return True
    except OSError as exc:
        if verbose:
            log(f"telnet BOOTSEL failed on {host}:{port}: {exc}")
        return False


def telnet_immediate_bootsel(host: str, port: int, picotool: str,
                             bootsel_timeout: float, verbose: bool) -> bool:
    payloads = (
        b"bootsel 250\n",
        b"fruitjamctl bootsel 250\n",
        b"/usr/bin/fruitjamctl bootsel 250\n",
        b"reboot bootsel\n",
        b"\r\nbootsel 250\r\n",
        b"\r\n/usr/bin/fruitjamctl bootsel 250\r\n",
        b"\r\nfruitjamctl bootsel 250\r\n",
    )
    sent = False

    for attempt, payload in enumerate(payloads, start=1):
        try:
            with socket.create_connection((host, port), timeout=2.0) as sock:
                sock.settimeout(0.2)
                sock.sendall(payload)
                sent = True
                log(f"sent immediate BOOTSEL command over telnet {host}:{port} attempt {attempt}")
                try:
                    data = sock.recv(256)
                except socket.timeout:
                    data = b""
                if verbose and data:
                    log(data.decode("utf-8", "replace").rstrip())
        except OSError as exc:
            if verbose:
                log(f"immediate telnet BOOTSEL attempt {attempt} failed on {host}:{port}: {exc}")
            time.sleep(0.25)
        if bootsel_timeout > 0 and wait_for_bootsel(picotool, bootsel_timeout, verbose):
            return True

    return sent


def request_bootsel(args: argparse.Namespace) -> bool:
    if picotool_info(args.picotool, args.verbose):
        log("RP2350 is already in BOOTSEL")
        return True

    if not args.skip_picotool_force:
        if picotool_force_bootsel(args.picotool, args.verbose):
            if wait_for_bootsel(args.picotool, post_trigger_bootsel_timeout(args), args.verbose):
                return True

    hosts = recovery_hosts(args)
    if not hosts:
        log("no AirLift recovery hosts found")

    if not args.skip_http:
        for host in hosts:
            if http_bootsel(host, args.http_port, args.verbose):
                if wait_for_bootsel(args.picotool, post_trigger_bootsel_timeout(args), args.verbose):
                    return True

    if not args.skip_telnet:
        for host in hosts:
            if telnet_immediate_bootsel(host, args.telnet_port, args.picotool,
                                        args.bootsel_timeout, args.verbose):
                if wait_for_bootsel(args.picotool, post_trigger_bootsel_timeout(args), args.verbose):
                    return True
            if telnet_bootsel(host, args.telnet_port, args.verbose):
                if wait_for_bootsel(args.picotool, post_trigger_bootsel_timeout(args), args.verbose):
                    return True

    uart_ports = selected_uart_ports(args.uart_port, args.uart_discovery, args.verbose)
    if not args.skip_uart and not uart_ports:
        log("no UART ports found for shell recovery")
    if not args.skip_uart:
        for port in uart_ports:
            if uart_shell_bootsel(port, args.uart_baud, args.serial_open_timeout, args.verbose):
                if wait_for_bootsel(args.picotool, post_trigger_bootsel_timeout(args), args.verbose):
                    return True

    ports = selected_cdc_ports(args.port, args.include_tty_counterpart)
    if not ports:
        log("no CDC ports found for shell recovery")

    if not args.skip_cdc_usb_control:
        if cdc_usb_control_bootsel(args.verbose):
            if wait_for_bootsel(args.picotool, post_trigger_bootsel_timeout(args), args.verbose):
                return True

    for port in ports:
        if not args.skip_cdc_1200_touch:
            if cdc_1200_native_touch_bootsel(port, args.serial_open_timeout, args.verbose):
                if wait_for_bootsel(args.picotool, post_trigger_bootsel_timeout(args), args.verbose):
                    return True

        if not args.skip_cdc_shell:
            if cdc_shell_bootsel(port, args.baud, args.serial_open_timeout, args.verbose):
                if wait_for_bootsel(args.picotool, post_trigger_bootsel_timeout(args), args.verbose):
                    return True

        if not args.skip_cdc_raw_shell:
            if cdc_raw_shell_bootsel(port, args.serial_open_timeout, args.verbose):
                if wait_for_bootsel(args.picotool, post_trigger_bootsel_timeout(args), args.verbose):
                    return True

        if not args.skip_cdc_1200_touch:
            if cdc_1200_stty_bootsel(port, args.serial_open_timeout, args.verbose):
                if wait_for_bootsel(args.picotool, post_trigger_bootsel_timeout(args), args.verbose):
                    return True
            if cdc_1200_touch_bootsel(port, args.serial_open_timeout, args.verbose):
                if wait_for_bootsel(args.picotool, post_trigger_bootsel_timeout(args), args.verbose):
                    return True

    return picotool_info(args.picotool, args.verbose)


def notify_bark(url: str, title: str, body: str, verbose: bool) -> None:
    if not url:
        if verbose:
            log("Bark notification skipped: no FJ_BARK_URL/BARK_URL configured")
        return
    parts = urlsplit(url)
    path_segments = [segment for segment in parts.path.split("/") if segment]
    if len(path_segments) >= 3:
        target = url
    else:
        path = parts.path.rstrip("/")
        if len(path_segments) <= 1:
            path += f"/{quote(title, safe='')}"
        path += f"/{quote(body, safe='')}"
        target = urlunsplit((parts.scheme, parts.netloc, path, parts.query, parts.fragment))
    request = Request(target, headers={"User-Agent": "fruitjam-recover-flash/1"})
    try:
        with urlopen(request, timeout=5.0) as response:
            response.read(256)
        log("sent Bark BOOTSEL notification")
    except OSError as exc:
        if verbose:
            log(f"Bark BOOTSEL notification failed: {exc}")
        curl = shutil.which("curl")
        if not curl:
            return
        try:
            result = subprocess.run(
                [curl, "-fsS", "--max-time", "10", target],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=12.0,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as curl_exc:
            if verbose:
                log(f"Bark curl fallback failed: {curl_exc}")
            return
        if result.returncode == 0:
            log("sent Bark BOOTSEL notification with curl")
        elif verbose:
            detail = result.stdout.strip() or f"exit {result.returncode}"
            log(f"Bark curl fallback failed: {detail}")


def watch_only_bootsel(args: argparse.Namespace) -> bool:
    if picotool_info(args.picotool, args.verbose):
        log("RP2350 is already in BOOTSEL")
        return True

    timeout = args.manual_bootsel_timeout
    if timeout <= 0:
        log("watch-only BOOTSEL polling disabled by --manual-bootsel-timeout 0")
        return False

    log(f"watch-only: waiting up to {timeout:g}s for BOOTSEL; no recovery triggers will be sent")
    notify_bark(
        args.bark_url,
        "Fruit Jam BOOTSEL watch",
        "Waiting for RP2350 ROM BOOTSEL; no HTTP, telnet, CDC, or 1200-baud triggers will be sent.",
        args.verbose,
    )
    return wait_for_bootsel(args.picotool, timeout, args.verbose)


def flash_uf2(args: argparse.Namespace) -> int:
    uf2 = Path(args.uf2)
    if not uf2.is_file():
        log(f"missing UF2: {uf2}")
        return 2
    load = run_cmd([args.picotool, "load", "-fu", str(uf2)], timeout=args.flash_timeout, verbose=True)
    if load.stdout.strip():
        log(load.stdout.rstrip())
    if load.returncode != 0:
        return load.returncode
    if args.no_reboot:
        return 0
    reboot = run_cmd([args.picotool, "reboot"], timeout=15.0, verbose=True)
    if reboot.stdout.strip():
        log(reboot.stdout.rstrip())
    return reboot.returncode


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uf2", default=str(DEFAULT_UF2), help="UF2 to flash after BOOTSEL is visible")
    parser.add_argument("--picotool", default=DEFAULT_PICOTOOL, help="picotool executable")
    parser.add_argument("--port", help="CDC port; defaults to FJ_CDC_PORT or /dev/cu.usbmodem*")
    parser.add_argument("--baud", type=int, default=int(os.environ.get("FJ_CDC_BAUD", "115200")))
    parser.add_argument("--serial-open-timeout", type=float, default=6.0)
    parser.add_argument("--bootsel-timeout", type=float, default=12.0)
    parser.add_argument("--post-trigger-bootsel-timeout", type=float,
                        default=DEFAULT_POST_TRIGGER_BOOTSEL_TIMEOUT,
                        help="seconds to wait after a recovery trigger was accepted")
    parser.add_argument("--manual-bootsel-timeout", type=float,
                        default=DEFAULT_MANUAL_BOOTSEL_TIMEOUT,
                        help="seconds to keep polling after all automatic recovery routes fail")
    parser.add_argument("--flash-timeout", type=float, default=DEFAULT_FLASH_TIMEOUT)
    parser.add_argument("--http-host", default=DEFAULT_HTTP_HOST)
    parser.add_argument("--http-port", type=int, default=DEFAULT_HTTP_PORT)
    parser.add_argument("--telnet-host", default=DEFAULT_TELNET_HOST)
    parser.add_argument("--telnet-port", type=int, default=DEFAULT_TELNET_PORT)
    parser.add_argument("--uart-port", default=DEFAULT_UART_PORT,
                        help="hardware UART serial port; defaults to FJ_UART_PORT or discovered /dev/cu.usbserial*")
    parser.add_argument("--uart-baud", type=int, default=DEFAULT_UART_BAUD)
    parser.add_argument("--airlift-discovery", action="store_true",
                        default=DEFAULT_AIRLIFT_DISCOVERY,
                        help="discover esp32c6/Fruit Jam AirLift hosts from the ARP cache")
    parser.add_argument("--skip-airlift-discovery", action="store_false",
                        dest="airlift_discovery",
                        help="do not discover AirLift hosts from the ARP cache")
    parser.add_argument("--skip-http", action="store_true")
    parser.add_argument("--skip-telnet", action="store_true")
    parser.add_argument("--skip-picotool-force", action="store_true",
                        help="do not try 'picotool reboot -u -f' before service fallbacks")
    parser.add_argument("--skip-uart", action="store_true")
    parser.add_argument("--uart-discovery", action="store_true",
                        default=DEFAULT_UART_DISCOVERY,
                        help="discover likely USB UART adapters from /dev")
    parser.add_argument("--skip-uart-discovery", action="store_false",
                        dest="uart_discovery",
                        help="do not discover USB UART adapters from /dev")
    parser.add_argument("--skip-cdc-shell", action="store_true")
    parser.add_argument("--skip-cdc-raw-shell", action="store_true")
    parser.add_argument("--skip-cdc-usb-control", action="store_true")
    parser.add_argument("--skip-cdc-1200-touch", action="store_true")
    parser.add_argument("--include-tty-counterpart", action="store_true",
                        default=DEFAULT_INCLUDE_TTY_COUNTERPART,
                        help="also try the /dev/tty.* peer for a selected /dev/cu.* CDC port")
    parser.add_argument("--no-tty-counterpart", action="store_false",
                        dest="include_tty_counterpart",
                        help="do not try the /dev/tty.* peer for a selected /dev/cu.* CDC port")
    parser.add_argument("--watch-only", action="store_true",
                        help="only poll for an already/manual BOOTSEL device; do not touch HTTP, telnet, or CDC")
    parser.add_argument("--bark-url", default=DEFAULT_BARK_URL,
                        help="Bark endpoint used to notify when manual BOOTSEL is needed")
    parser.add_argument("--no-flash", action="store_true", help="stop after proving BOOTSEL is visible")
    parser.add_argument("--no-reboot", action="store_true", help="leave the board in BOOTSEL after flashing")
    parser.add_argument("-v", "--verbose", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.watch_only:
            bootsel_ready = watch_only_bootsel(args)
        else:
            bootsel_ready = request_bootsel(args)

        if not bootsel_ready:
            log("RP2350 BOOTSEL did not become visible")
            if not args.watch_only:
                notify_bark(
                    args.bark_url,
                    "Fruit Jam BOOTSEL needed",
                    "USB CDC, telnet, HTTP, and 1200-baud recovery did not expose BOOTSEL.",
                    args.verbose,
                )
            if not args.watch_only and args.manual_bootsel_timeout > 0:
                log(f"waiting up to {args.manual_bootsel_timeout:g}s for BOOTSEL to appear")
                if wait_for_bootsel(args.picotool, args.manual_bootsel_timeout, args.verbose):
                    log("BOOTSEL appeared during fallback wait")
                else:
                    return 2
            else:
                return 2
        if args.no_flash:
            log("BOOTSEL visible; no flash requested")
            return 0
        return flash_uf2(args)
    except KeyboardInterrupt:
        log("interrupted")
        return 130
    except (RuntimeError, subprocess.TimeoutExpired) as exc:
        log(str(exc))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
