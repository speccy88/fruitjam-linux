#!/usr/bin/env python3
"""Record and analyze Fruit Jam audio with the Mac microphone.

This helper is intentionally host-side. It can:
  * record a short microphone sample with ffmpeg/AVFoundation,
  * trigger a Fruit Jam shell command over UART or telnet,
  * analyze the recording for the expected RTTTL note frequencies, and
  * generate/install a tiny WAV sample for fruitjam-wavplay tests.
"""

from __future__ import annotations

import argparse
import base64
from ftplib import FTP, all_errors
import json
import math
import os
import re
import shlex
import socket
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

import numpy as np


RTTTL_BUILTINS = {
    "default": "fruit:d=8,o=5,b=120:c,e,g,c6,g,e,c",
    "scale": "scale:d=8,o=5,b=140:c,d,e,f,g,a,b,c6",
    "startup": "start:d=16,o=5,b=180:c,e,g,c6,8p,g,c6",
    "retro": "retro:d=16,o=5,b=200:c6,g,e,c,g,c6,8p,c6,d6,e6,8g6",
    "chime": "chime:d=8,o=6,b=120:c,e,g,2c7",
}

NOTE_C4 = {
    "c": 262,
    "c#": 277,
    "d": 294,
    "d#": 311,
    "e": 330,
    "f": 349,
    "f#": 370,
    "g": 392,
    "g#": 415,
    "a": 440,
    "a#": 466,
    "b": 494,
}


def avfoundation_audio_devices() -> list[tuple[int, str]]:
    proc = subprocess.run(
        [
            "ffmpeg",
            "-hide_banner",
            "-f",
            "avfoundation",
            "-list_devices",
            "true",
            "-i",
            "",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    devices: list[tuple[int, str]] = []
    in_audio = False
    pattern = re.compile(r"\[(\d+)\]\s+(.+)$")
    for line in proc.stderr.splitlines():
        if "AVFoundation audio devices:" in line:
            in_audio = True
            continue
        if "AVFoundation video devices:" in line:
            in_audio = False
            continue
        if not in_audio:
            continue
        match = pattern.search(line)
        if match:
            devices.append((int(match.group(1)), match.group(2).strip()))
    return devices


def resolve_mic_device(value: str) -> str:
    if value != "auto":
        if value.isdigit():
            return f":{value}"
        return value

    devices = avfoundation_audio_devices()
    if not devices:
        raise RuntimeError("no AVFoundation audio devices found")

    def score(name: str) -> int:
        lower = name.lower()
        total = 0
        for token in ("micro macbook", "macbook", "built-in", "internal", "microphone"):
            if token in lower:
                total += 10
        for token in ("teams", "iphone", "blackhole", "loopback", "zoom"):
            if token in lower:
                total -= 20
        return total

    index, name = max(devices, key=lambda item: (score(item[1]), -item[0]))
    print(f"mic device: :{index} ({name})", file=sys.stderr)
    return f":{index}"


def resolve_rtttl(value: str) -> str:
    if value in RTTTL_BUILTINS:
        return RTTTL_BUILTINS[value]
    path = Path(value)
    if ":" not in value and path.exists():
        return path.read_text(encoding="utf-8").splitlines()[0].strip()
    return value


def parse_uint(text: str, idx: int) -> tuple[int | None, int]:
    start = idx
    value = 0
    while idx < len(text) and text[idx].isdigit():
        value = value * 10 + ord(text[idx]) - ord("0")
        idx += 1
    if idx == start:
        return None, idx
    return value, idx


def note_hz(name: str, sharp: bool, octave: int) -> int:
    key = name.lower() + ("#" if sharp else "")
    if key not in NOTE_C4:
        return 0
    hz = NOTE_C4[key]
    while octave > 4:
        hz *= 2
        octave -= 1
    while octave < 4:
        hz = (hz + 1) // 2
        octave += 1
    return hz


def parse_rtttl(text: str) -> list[tuple[int, int]]:
    parts = text.strip().split(":", 2)
    if len(parts) != 3:
        raise ValueError("RTTTL must contain name:defaults:notes")

    defaults = {"d": 4, "o": 5, "b": 120}
    for item in parts[1].split(","):
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        key = key.strip().lower()
        if key in defaults:
            defaults[key] = max(1, int(value))

    whole = (60000 * 4) // defaults["b"]
    notes: list[tuple[int, int]] = []
    idx = 0
    stream = parts[2].strip()
    while idx < len(stream):
        while idx < len(stream) and stream[idx] in " ,\t\r\n":
            idx += 1
        if idx >= len(stream):
            break

        dur, idx = parse_uint(stream, idx)
        if dur is None:
            dur = defaults["d"]
        if idx >= len(stream):
            break

        name = stream[idx].lower()
        idx += 1
        sharp = False
        dotted = False
        if idx < len(stream) and stream[idx] == "#":
            sharp = True
            idx += 1
        if idx < len(stream) and stream[idx] == ".":
            dotted = True
            idx += 1

        octave, idx = parse_uint(stream, idx)
        if octave is None:
            octave = defaults["o"]
        if idx < len(stream) and stream[idx] == ".":
            dotted = True
            idx += 1

        ms = max(1, whole // max(1, dur))
        if dotted:
            ms += ms // 2
        notes.append((0 if name == "p" else note_hz(name, sharp, octave), ms))

        while idx < len(stream) and stream[idx] != ",":
            idx += 1
    return notes


def generate_wav(path: Path, rtttl: str, sample_rate: int = 8000) -> None:
    notes = parse_rtttl(rtttl)
    fade = max(1, sample_rate // 200)
    samples: list[int] = []
    phase = 0.0
    for hz, ms in notes:
        count = max(1, round(sample_rate * ms / 1000))
        step = (2.0 * math.pi * hz / sample_rate) if hz else 0.0
        for i in range(count):
            amp = 0.45
            if i < fade:
                amp *= i / fade
            if count - i < fade:
                amp *= max(0, count - i) / fade
            if hz:
                sample = int(math.sin(phase) * amp * 32767)
                phase += step
            else:
                sample = 0
            samples.append(max(-32768, min(32767, sample)))
        samples.extend([0] * max(1, sample_rate // 125))

    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(b"".join(struct.pack("<h", s) for s in samples))


def send_serial(port: str, script: str, read_time: float) -> str:
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("pyserial is required for --serial-port") from exc

    out = bytearray()
    with serial.Serial(port, 115200, timeout=0.2, write_timeout=5) as ser:
        ser.dtr = False
        ser.rts = False
        time.sleep(0.2)
        ser.reset_input_buffer()
        payload = script.encode("utf-8") + b"\n"
        if len(payload) > 1024:
            for line in payload.splitlines(keepends=True):
                ser.write(line)
                ser.flush()
                time.sleep(0.01)
        else:
            ser.write(payload)
            ser.flush()
        end = time.time() + read_time
        while time.time() < end:
            waiting = ser.in_waiting
            if waiting:
                out.extend(ser.read(waiting))
                end = time.time() + min(1.0, read_time)
            time.sleep(0.05)
    return out.decode("utf-8", "replace")


def send_telnet(host: str, port: int, script: str, read_time: float) -> str:
    out = bytearray()
    with socket.create_connection((host, port), timeout=4) as sock:
        sock.settimeout(0.35)
        try:
            out.extend(sock.recv(1024))
        except OSError:
            pass
        sock.sendall(script.encode("utf-8") + b"\n")
        end = time.time() + read_time
        while time.time() < end:
            try:
                chunk = sock.recv(1024)
            except OSError:
                time.sleep(0.05)
                continue
            if not chunk:
                break
            out.extend(chunk)
    return out.decode("utf-8", "replace")


def send_board_script(args: argparse.Namespace, script: str, read_time: float) -> str:
    if args.serial_port:
        return send_serial(args.serial_port, script, read_time)
    if args.host:
        return send_telnet(args.host, args.telnet_port, script, read_time)
    raise RuntimeError("use --serial-port or --host to talk to the board")


def record_with_trigger(args: argparse.Namespace, command: str) -> None:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    mic_device = resolve_mic_device(args.mic_device)
    ffmpeg_cmd = [
        "ffmpeg",
        "-hide_banner",
        "-nostdin",
        "-f",
        "avfoundation",
        "-i",
        mic_device,
        "-t",
        f"{args.duration:.3f}",
        "-ac",
        "1",
        "-ar",
        "44100",
        "-y",
        str(args.output),
    ]
    proc = subprocess.Popen(
        ffmpeg_cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    time.sleep(args.lead_in)
    trigger_output = ""
    if command:
        trigger_output = send_board_script(args, command, args.command_read_time)
    _, stderr = proc.communicate()
    if proc.returncode != 0:
        raise RuntimeError(f"ffmpeg failed with {proc.returncode}:\n{stderr}")
    if trigger_output.strip():
        print("board output:")
        print(trigger_output.rstrip())


def load_wav(path: Path) -> tuple[int, np.ndarray]:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        width = wav.getsampwidth()
        rate = wav.getframerate()
        frames = wav.readframes(wav.getnframes())

    if width == 2:
        data = np.frombuffer(frames, dtype="<i2").astype(np.float32) / 32768.0
    elif width == 1:
        data = (np.frombuffer(frames, dtype=np.uint8).astype(np.float32) - 128.0) / 128.0
    else:
        raise ValueError(f"unsupported WAV sample width {width}")
    if channels > 1:
        data = data.reshape((-1, channels)).mean(axis=1)
    return rate, data


def analyze_recording(
    path: Path, expected_notes: list[tuple[int, int]], lead_in: float, min_matches: int
) -> dict:
    rate, data = load_wav(path)
    expected = sorted({hz for hz, _ in expected_notes if hz > 0})
    if not expected:
        raise ValueError("expected RTTTL has no audible notes")

    win = min(4096, max(1024, 1 << int(math.log2(max(1024, rate // 10)))))
    hop = max(256, win // 4)
    if data.size < win:
        raise ValueError("recording too short to analyze")

    window = np.hanning(win).astype(np.float32)
    freqs = np.fft.rfftfreq(win, 1.0 / rate)
    freq_mask = (freqs >= 80.0) & (freqs <= 3000.0)
    lead_frames = max(1, int(lead_in * rate))
    baseline = data[: min(lead_frames, data.size)]
    baseline_rms = float(np.sqrt(np.mean(np.square(baseline)))) if baseline.size else 0.0

    frame_rms: list[float] = []
    peaks: list[float] = []
    matched_frames = {hz: 0 for hz in expected}
    matches = {hz: False for hz in expected}
    active_cut = 0.0

    starts = range(0, data.size - win + 1, hop)
    spectra: list[tuple[int, float, np.ndarray]] = []
    for start in starts:
        frame = data[start : start + win]
        rms = float(np.sqrt(np.mean(np.square(frame))))
        frame_rms.append(rms)
        spec = np.abs(np.fft.rfft(frame * window))
        spectra.append((start, rms, spec))

    if frame_rms:
        active_cut = max(
            baseline_rms * 3.0,
            float(np.percentile(frame_rms, 75)) * 0.40,
            0.0005,
        )

    matched_active_frames = 0
    for start, rms, spec in spectra:
        if start < lead_frames:
            continue
        masked = spec[freq_mask]
        if masked.size == 0:
            continue
        frame_peak = float(masked.max())
        if frame_peak <= 0:
            continue
        peak_freq = float(freqs[freq_mask][int(masked.argmax())])
        peaks.append(peak_freq)
        floor = float(np.median(masked))
        for hz in expected:
            tol = max(28.0, hz * 0.08)
            band = (freqs >= hz - tol) & (freqs <= hz + tol)
            if not band.any():
                continue
            band_peak = float(spec[band].max())
            band_snr = band_peak / max(floor, 1e-9)
            global_peak_is_note = abs(peak_freq - hz) <= tol
            strong_note_band = band_peak >= frame_peak * 0.48 and band_snr >= 8.0
            if global_peak_is_note or strong_note_band:
                matches[hz] = True
                matched_frames[hz] += 1
                matched_active_frames += 1

    active_rms = [r for r in frame_rms if r >= active_cut]
    peak_rms = max(frame_rms) if frame_rms else 0.0
    gain_db = 99.0 if baseline_rms <= 1e-7 and peak_rms > 0 else (
        20.0 * math.log10(max(peak_rms, 1e-7) / max(baseline_rms, 1e-7))
    )
    matched = sorted(hz for hz, ok in matches.items() if ok)
    required_active_frames = max(6, min(min_matches * 2, 12))
    pass_audio = (
        len(matched) >= min(min_matches, len(expected))
        and matched_active_frames >= required_active_frames
        and peak_rms >= 0.0002
    )

    return {
        "path": str(path),
        "sample_rate": rate,
        "duration_s": round(data.size / rate, 3),
        "baseline_rms": baseline_rms,
        "peak_rms": peak_rms,
        "gain_db": gain_db,
        "active_threshold": active_cut,
        "active_frames": len(active_rms),
        "matched_active_frames": matched_active_frames,
        "required_active_frames": required_active_frames,
        "expected_hz": expected,
        "matched_hz": matched,
        "matched_frames": {str(hz): matched_frames[hz] for hz in expected if matched_frames[hz]},
        "dominant_peaks_hz": [round(p, 1) for p in peaks[:40]],
        "pass": pass_audio,
    }


def analyze_audio_presence(
    path: Path,
    lead_in: float,
    min_gain_db: float,
    min_peak_rms: float,
    min_active_frames: int,
) -> dict:
    rate, data = load_wav(path)
    win = min(4096, max(1024, 1 << int(math.log2(max(1024, rate // 10)))))
    hop = max(256, win // 4)
    if data.size < win:
        raise ValueError("recording too short to analyze")

    window = np.hanning(win).astype(np.float32)
    freqs = np.fft.rfftfreq(win, 1.0 / rate)
    freq_mask = (freqs >= 80.0) & (freqs <= 5000.0)
    lead_frames = max(1, int(lead_in * rate))
    baseline = data[: min(lead_frames, data.size)]
    baseline_rms = float(np.sqrt(np.mean(np.square(baseline)))) if baseline.size else 0.0

    frame_rms: list[float] = []
    peaks: list[float] = []
    for start in range(0, data.size - win + 1, hop):
        frame = data[start : start + win]
        rms = float(np.sqrt(np.mean(np.square(frame))))
        frame_rms.append(rms)
        if start < lead_frames:
            continue
        spec = np.abs(np.fft.rfft(frame * window))
        masked = spec[freq_mask]
        if masked.size and rms > baseline_rms * 2.0:
            peaks.append(float(freqs[freq_mask][int(masked.argmax())]))

    peak_rms = max(frame_rms) if frame_rms else 0.0
    active_cut = max(baseline_rms * 3.0, min_peak_rms)
    active_frames = sum(1 for rms in frame_rms if rms >= active_cut)
    gain_db = 99.0 if baseline_rms <= 1e-7 and peak_rms > 0 else (
        20.0 * math.log10(max(peak_rms, 1e-7) / max(baseline_rms, 1e-7))
    )
    pass_audio = (
        peak_rms >= min_peak_rms
        and gain_db >= min_gain_db
        and active_frames >= min_active_frames
    )

    return {
        "path": str(path),
        "sample_rate": rate,
        "duration_s": round(data.size / rate, 3),
        "baseline_rms": baseline_rms,
        "peak_rms": peak_rms,
        "gain_db": gain_db,
        "active_threshold": active_cut,
        "active_frames": active_frames,
        "required_active_frames": min_active_frames,
        "dominant_peaks_hz": [round(p, 1) for p in peaks[:40]],
        "pass": pass_audio,
    }


def base32_text(path: Path) -> str:
    encoded = base64.b32encode(path.read_bytes()).decode("ascii")
    return "\n".join(encoded[i : i + 64] for i in range(0, len(encoded), 64))


def install_wav_to_sd(args: argparse.Namespace, wav_path: Path, remote: str) -> None:
    remote_path = shlex.quote(remote)
    remote_parent = str(Path(remote).parent)
    mkdir_parts = ["/tmp"]
    if remote_parent and remote_parent != ".":
        mkdir_parts.insert(0, shlex.quote(remote_parent))
    script = "\n".join(
        [
            "mkdir -p " + " ".join(mkdir_parts),
            "rm -f /tmp/fruitjam-sample.wav.b32",
            f"cat > /tmp/fruitjam-sample.wav.b32 <<'FJ_WAV_B32'",
            base32_text(wav_path),
            "FJ_WAV_B32",
            f"base32 -d /tmp/fruitjam-sample.wav.b32 > {remote_path}",
            "rm -f /tmp/fruitjam-sample.wav.b32",
            f"ls -l {remote_path}",
        ]
    )
    print(send_board_script(args, script, args.upload_read_time).rstrip())


def ftp_path_for_remote(remote: str, root: str) -> str:
    root = root.rstrip("/")
    if root and remote.startswith(root + "/"):
        return remote[len(root) + 1 :]
    if remote.startswith("/"):
        raise ValueError(
            f"{remote} is outside FTP root {root}; pass --ftp-path explicitly"
        )
    return remote


def install_wav_by_ftp(args: argparse.Namespace, wav_path: Path, remote: str) -> None:
    ftp_path = args.ftp_path or ftp_path_for_remote(remote, args.ftp_root)
    ftp_path = ftp_path.lstrip("/")
    parent = str(Path(ftp_path).parent)

    with FTP() as ftp:
        ftp.connect(args.ftp_host, args.ftp_port, timeout=args.ftp_timeout)
        ftp.login("anonymous", "fruitjam@example.local")
        ftp.set_pasv(not args.ftp_active)
        if parent and parent != ".":
            current = ""
            for part in parent.split("/"):
                current = f"{current}/{part}" if current else part
                try:
                    ftp.mkd(current)
                except all_errors:
                    pass
        with wav_path.open("rb") as f:
            ftp.storbinary(f"STOR {ftp_path}", f, blocksize=4096)
    print(f"uploaded {wav_path} to ftp://{args.ftp_host}/{ftp_path}")


def default_command(args: argparse.Namespace) -> str:
    if args.no_trigger:
        return ""
    prefix = ""
    if args.pulse_periph_reset:
        prefix = "fruitjamctl periph-reset pulse\nsleep 1\n"
    if args.command:
        return prefix + args.command
    loud = "--loud " if args.loud else ""
    if args.tone_hz is not None:
        return prefix + f"fruitjam-rtttl {loud}--tone {args.tone_hz} {args.tone_ms}"
    return prefix + f"fruitjam-rtttl {loud}{args.rtttl}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", help="Fruit Jam IP for telnet trigger")
    parser.add_argument("--telnet-port", type=int, default=23)
    parser.add_argument("--serial-port", help="Fruit Jam UART/CDC device for trigger")
    parser.add_argument(
        "--mic-device",
        default="auto",
        help="ffmpeg AVFoundation mic device, or auto to prefer the Mac microphone",
    )
    parser.add_argument("--list-mics", action="store_true", help="list AVFoundation audio devices and exit")
    parser.add_argument("--duration", type=float, default=8.0)
    parser.add_argument("--lead-in", type=float)
    parser.add_argument("--command-read-time", type=float, default=0.5)
    parser.add_argument("--upload-read-time", type=float, default=12.0)
    parser.add_argument("--rtttl", default="scale", help="built-in name, RTTTL text, or file")
    parser.add_argument("--tone-hz", type=int, help="ask the board to play one calibration tone")
    parser.add_argument("--tone-ms", type=int, default=2500, help="duration for --tone-hz")
    parser.add_argument("--command", help="board command to run while recording")
    parser.add_argument("--loud", action="store_true", help="request louder target-side calibration output")
    parser.add_argument(
        "--pulse-periph-reset",
        action="store_true",
        help="pulse shared TLV320/ESP32-C6 reset before playback; serial trigger only",
    )
    parser.add_argument("--no-trigger", action="store_true", help="record without sending a board command")
    parser.add_argument("--output", type=Path, default=Path("/tmp/fruitjam-rtttl-mic.wav"))
    parser.add_argument("--analyze-only", type=Path, help="analyze an existing mic WAV")
    parser.add_argument("--record-only", action="store_true")
    parser.add_argument("--make-sample-wav", type=Path, help="write a generated WAV and exit")
    parser.add_argument("--install-sd-wav", action="store_true")
    parser.add_argument(
        "--remote-wav",
        default="/mnt/sd/wavs/fruitjam-scale.wav",
        help="target path used with --install-sd-wav",
    )
    parser.add_argument("--ftp-host", help="install generated WAV through Fruit Jam FTP")
    parser.add_argument("--ftp-port", type=int, default=21)
    parser.add_argument("--ftp-timeout", type=float, default=20.0)
    parser.add_argument("--ftp-root", default="/mnt/sd")
    parser.add_argument("--ftp-path", help="FTP pathname for --install-sd-wav")
    parser.add_argument("--ftp-active", action="store_true", help="use active FTP instead of passive")
    parser.add_argument("--run-wavplay", action="store_true")
    parser.add_argument("--min-matches", type=int, default=4)
    parser.add_argument(
        "--expect-audio",
        action="store_true",
        help="pass on audible energy instead of matching RTTTL note frequencies",
    )
    parser.add_argument("--min-gain-db", type=float, default=6.0)
    parser.add_argument("--min-peak-rms", type=float, default=0.001)
    parser.add_argument("--min-active-frames", type=int, default=8)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if args.lead_in is None:
        args.lead_in = 0.0 if args.analyze_only else 1.0

    if args.pulse_periph_reset and not args.serial_port:
        parser.error("--pulse-periph-reset is only safe with --serial-port")
    if args.tone_hz is not None and args.tone_hz < 80:
        parser.error("--tone-hz must be at least 80 Hz for microphone analysis")
    if args.tone_hz is not None and args.tone_hz > 3000:
        parser.error("--tone-hz must be 3000 Hz or less for microphone analysis")
    if args.tone_ms < 50:
        parser.error("--tone-ms must be at least 50")
    if args.tone_hz is not None and (args.make_sample_wav or args.install_sd_wav):
        parser.error("--tone-hz is for live board calibration, not generated WAV install")

    if args.list_mics:
        for index, name in avfoundation_audio_devices():
            print(f":{index} {name}")
        return 0

    rtttl = resolve_rtttl(args.rtttl)
    expected_notes = (
        [(args.tone_hz, args.tone_ms)]
        if args.tone_hz is not None
        else parse_rtttl(rtttl)
    )

    if args.make_sample_wav:
        generate_wav(args.make_sample_wav, rtttl)
        print(args.make_sample_wav)
        return 0

    if args.install_sd_wav:
        with tempfile.TemporaryDirectory() as td:
            wav_path = Path(td) / "fruitjam-scale.wav"
            generate_wav(wav_path, rtttl)
            if args.ftp_host:
                install_wav_by_ftp(args, wav_path, args.remote_wav)
            else:
                install_wav_to_sd(args, wav_path, args.remote_wav)
        if not args.run_wavplay and not args.record_only:
            return 0

    if args.run_wavplay:
        loud = "--loud " if args.loud else ""
        args.command = f"fruitjam-wavplay {loud}{args.remote_wav}"

    if args.analyze_only:
        if args.expect_audio:
            result = analyze_audio_presence(
                args.analyze_only,
                args.lead_in,
                args.min_gain_db,
                args.min_peak_rms,
                args.min_active_frames,
            )
        else:
            result = analyze_recording(
                args.analyze_only, expected_notes, args.lead_in, args.min_matches
            )
    else:
        record_with_trigger(args, default_command(args))
        if args.record_only:
            print(args.output)
            return 0
        if args.expect_audio:
            result = analyze_audio_presence(
                args.output,
                args.lead_in,
                args.min_gain_db,
                args.min_peak_rms,
                args.min_active_frames,
            )
        else:
            result = analyze_recording(args.output, expected_notes, args.lead_in, args.min_matches)

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(f"recording: {result['path']}")
        print(f"duration: {result['duration_s']} s")
        print(f"rms: baseline={result['baseline_rms']:.6f} peak={result['peak_rms']:.6f} gain={result['gain_db']:.1f} dB")
        if "expected_hz" in result:
            print(f"expected Hz: {result['expected_hz']}")
            print(f"matched Hz: {result['matched_hz']}")
        else:
            print(f"active frames: {result['active_frames']}/{result['required_active_frames']}")
        print(f"dominant peaks Hz: {result['dominant_peaks_hz'][:16]}")
        print("PASS" if result["pass"] else "FAIL")
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
