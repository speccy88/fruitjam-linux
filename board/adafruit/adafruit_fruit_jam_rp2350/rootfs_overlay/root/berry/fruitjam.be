var fruitjam = module("fruitjam")

import time
import string
import os

fruitjam.paths = {
    "neopixels": "/dev/neopixels",
    "audio": "/dev/fruitjam-audio",
    "dvi": "/dev/fruitjam-dvi",
    "i2c": "/dev/i2c-0",
    "spi": "/dev/spidev0.0",
    "gpio": "/sys/class/gpio",
    "usbhost": "/dev/fruitjam-usbhost",
    "adc": "/sys/bus/platform/devices/400a0000.adc"
}

fruitjam.button_gpios = [
    ["button1", 0],
    ["button2", 4],
    ["button3", 5]
]

fruitjam.usbhost_gpios = {
    "power": 11,
    "dp": 1,
    "dm": 2
}

fruitjam.adc_channels = [
    ["A0/GPIO40", 0],
    ["A1/GPIO41", 1],
    ["temperature", 8]
]

fruitjam.dvi_commands = [
    "show",
    "start",
    "stop",
    "clear",
    "white",
    "test",
    "bars",
    "pattern"
]

fruitjam.i2c_default_device = "/dev/i2c-0"

fruitjam.hex_values = {
    "0": 0,
    "1": 1,
    "2": 2,
    "3": 3,
    "4": 4,
    "5": 5,
    "6": 6,
    "7": 7,
    "8": 8,
    "9": 9,
    "a": 10,
    "b": 11,
    "c": 12,
    "d": 13,
    "e": 14,
    "f": 15,
    "A": 10,
    "B": 11,
    "C": 12,
    "D": 13,
    "E": 14,
    "F": 15
}

fruitjam.hid_key_names = {
    40: "enter",
    41: "esc",
    42: "backspace",
    43: "tab",
    44: "space",
    45: "-",
    46: "=",
    47: "[",
    48: "]",
    49: "\\",
    50: "nonus",
    51: ";",
    52: "'",
    53: "`",
    54: ",",
    55: ".",
    56: "/",
    57: "capslock",
    58: "f1",
    59: "f2",
    60: "f3",
    61: "f4",
    62: "f5",
    63: "f6",
    64: "f7",
    65: "f8",
    66: "f9",
    67: "f10",
    68: "f11",
    69: "f12",
    79: "right",
    80: "left",
    81: "down",
    82: "up"
}

fruitjam.clamp = def(value, low, high)
    if value < low
        return low
    end
    if value > high
        return high
    end
    return value
end

fruitjam.pause_ms = def(ms)
    try
        time.sleep_ms(ms)
    except .. as e, m
        var start = time.clock()
        while (time.clock() - start) < (ms / 1000.0)
        end
    end
end

fruitjam.exists = def(path)
    try
        var f = open(path, "r")
        f.close()
        return true
    except .. as e, m
        return false
    end
end

fruitjam.read_text = def(path)
    var f = open(path, "r")
    var text = f.read()
    f.close()
    return text
end

fruitjam.write_text = def(path, text)
    var f = open(path, "w")
    f.write(text)
    f.close()
end

fruitjam.shell_quote = def(value)
    return "'" + string.replace(str(value), "'", "'\\''") + "'"
end

fruitjam.int_or = def(value, fallback)
    try
        return int(value)
    except .. as e, m
        return fallback
    end
end

fruitjam.key_value_lines = def(text)
    var out = {}
    for line : string.split(text, "\n")
        if line != ""
            var sep = string.find(line, " ")
            if sep > 0
                out[line[0..sep - 1]] = line[sep + 1..size(line)]
            end
        end
    end
    return out
end

fruitjam.device_status = def()
    return {
        "neopixels": fruitjam.exists(fruitjam.paths["neopixels"]),
        "audio": fruitjam.exists(fruitjam.paths["audio"]),
        "dvi": fruitjam.exists(fruitjam.paths["dvi"]),
        "i2c": fruitjam.exists(fruitjam.paths["i2c"]),
        "spi": fruitjam.exists(fruitjam.paths["spi"]),
        "usbhost": fruitjam.exists(fruitjam.paths["usbhost"])
    }
end

fruitjam.gpio_value_path = def(gpio)
    return fruitjam.paths["gpio"] + "/gpio" + str(gpio) + "/value"
end

fruitjam.gpio_direction_path = def(gpio)
    return fruitjam.paths["gpio"] + "/gpio" + str(gpio) + "/direction"
end

fruitjam.ensure_gpio = def(gpio)
    var path = fruitjam.gpio_value_path(gpio)
    if fruitjam.exists(path)
        return true
    end
    try
        fruitjam.write_text(fruitjam.paths["gpio"] + "/export", str(gpio) + "\n")
    except .. as e, m
    end
    for attempt : 0 .. 49
        if fruitjam.exists(path)
            return true
        end
        fruitjam.pause_ms(2)
    end
    return false
end

fruitjam.gpio_read = def(gpio, direction)
    if !fruitjam.ensure_gpio(gpio)
        return -1
    end
    if direction != nil
        try
            fruitjam.write_text(fruitjam.gpio_direction_path(gpio), direction)
        except .. as e, m
            return -1
        end
    end
    var value = fruitjam.read_text(fruitjam.gpio_value_path(gpio))
    if value[0] == "0"
        return 0
    end
    return 1
end

fruitjam.gpio_write = def(gpio, value)
    if !fruitjam.ensure_gpio(gpio)
        return false
    end
    fruitjam.write_text(fruitjam.gpio_direction_path(gpio), "out")
    if value
        fruitjam.write_text(fruitjam.gpio_value_path(gpio), "1")
    else
        fruitjam.write_text(fruitjam.gpio_value_path(gpio), "0")
    end
    return true
end

fruitjam.button_status = def(button)
    var name = button[0]
    var gpio = button[1]
    var value = fruitjam.gpio_read(gpio, "in")
    var state = "unknown"
    if value == 0
        state = "pressed"
    elif value == 1
        state = "released"
    end
    return {"name": name, "gpio": gpio, "value": value, "state": state}
end

fruitjam.buttons_status = def()
    var out = []
    for button : fruitjam.button_gpios
        out.push(fruitjam.button_status(button))
    end
    return out
end

fruitjam.usbhost_device = def(power, dp, dm)
    if power < 0
        return "unknown"
    elif power == 0
        return "power-off"
    elif dp < 0 || dm < 0
        return "unknown"
    elif dp == 0 && dm == 0
        return "no-device-or-reset"
    elif dp == 1 && dm == 0
        return "full-speed-device"
    elif dp == 0 && dm == 1
        return "low-speed-device"
    end
    return "invalid-both-lines-high"
end

fruitjam.usbhost_status_from_bridge = def(text)
    var kv = fruitjam.key_value_lines(text)
    var power = fruitjam.int_or(kv.find("power", "-1"), -1)
    var dp = fruitjam.int_or(kv.find("dp", "-1"), -1)
    var dm = fruitjam.int_or(kv.find("dm", "-1"), -1)
    var device = kv.find("device", fruitjam.usbhost_device(power, dp, dm))
    return {
        "power": power,
        "dp": dp,
        "dm": dm,
        "device": device,
        "present": fruitjam.int_or(kv.find("present", "0"), 0) != 0,
        "hid": fruitjam.int_or(kv.find("hid", "0"), 0) != 0,
        "driver": kv.find("driver", "kernel-line-state"),
        "bridge": kv.find("bridge", ""),
        "pio": fruitjam.int_or(kv.find("pio", "-1"), -1),
        "pio_ready": fruitjam.int_or(kv.find("pio_ready", "0"), 0) != 0,
        "pio_configured": fruitjam.int_or(kv.find("pio_configured", "0"), 0) != 0,
        "pio_program": kv.find("pio_program", ""),
        "packets": fruitjam.int_or(kv.find("packets", "0"), 0),
        "tx_errors": fruitjam.int_or(kv.find("tx_errors", "0"), 0),
        "last_tx_result": fruitjam.int_or(kv.find("last_tx_result", "0"), 0),
        "last_tx_len": fruitjam.int_or(kv.find("last_tx_len", "0"), 0),
        "rx_attempts": fruitjam.int_or(kv.find("rx_attempts", "0"), 0),
        "rx_errors": fruitjam.int_or(kv.find("rx_errors", "0"), 0),
        "last_rx_result": fruitjam.int_or(kv.find("last_rx_result", "0"), 0),
        "last_rx_len": fruitjam.int_or(kv.find("last_rx_len", "0"), 0),
        "last_rx_pid": fruitjam.int_or(kv.find("last_rx_pid", "0"), 0),
        "last_rx_hex": kv.find("last_rx_hex", ""),
        "probe_summary": kv.find("probe_summary", ""),
        "first_milestone": kv.find("first_milestone", "boot-protocol-keyboard"),
        "next": "pio-packet-io"
    }
end

fruitjam.usbhost_status_from_gpio = def()
    var power = fruitjam.gpio_read(fruitjam.usbhost_gpios["power"], nil)
    var dp = fruitjam.gpio_read(fruitjam.usbhost_gpios["dp"], "in")
    var dm = fruitjam.gpio_read(fruitjam.usbhost_gpios["dm"], "in")
    return {
        "power": power,
        "dp": dp,
        "dm": dm,
        "device": fruitjam.usbhost_device(power, dp, dm),
        "present": power > 0 && ((dp == 1 && dm == 0) || (dp == 0 && dm == 1)),
        "hid": false,
        "driver": "sysfs-line-state",
        "next": "pio-packet-io",
        "first_milestone": "boot-protocol-keyboard"
    }
end

fruitjam.usbhost_status = def()
    if fruitjam.exists(fruitjam.paths["usbhost"])
        try
            return fruitjam.usbhost_status_from_bridge(
                fruitjam.read_text(fruitjam.paths["usbhost"]))
        except .. as e, m
        end
    end
    return fruitjam.usbhost_status_from_gpio()
end

fruitjam.usbhost_kbd_target_args = def(addr, config, iface, ep)
    if addr == nil && config == nil && iface == nil && ep == nil
        return ""
    end
    var a = addr
    var c = config
    var i = iface
    var e = ep
    if a == nil
        a = 1
    end
    if c == nil
        c = 1
    end
    if i == nil
        i = 0
    end
    if e == nil
        e = 1
    end
    return " " + fruitjam.shell_quote(a) +
           " " + fruitjam.shell_quote(c) +
           " " + fruitjam.shell_quote(i) +
           " " + fruitjam.shell_quote(e)
end

fruitjam.usbhost_kbd_find_command = def()
    return "fruitjam-usbhost kbd-find"
end

fruitjam.usbhost_kbd_live_command = def(mode, seconds, addr, config, iface, ep)
    var cmd = "fruitjam-usbhost " + mode
    var target = fruitjam.usbhost_kbd_target_args(addr, config, iface, ep)
    if seconds != nil || target != ""
        var s = seconds
        if s == nil
            s = mode == "kbd-shell" ? 0 : 30
        end
        cmd += " " + fruitjam.shell_quote(s)
    end
    return cmd + target
end

fruitjam.usbhost_kbd_text_command = def(seconds, addr, config, iface, ep)
    return fruitjam.usbhost_kbd_live_command("kbd-text", seconds, addr, config, iface, ep)
end

fruitjam.usbhost_kbd_events_command = def(seconds, addr, config, iface, ep)
    return fruitjam.usbhost_kbd_live_command("kbd-events", seconds, addr, config, iface, ep)
end

fruitjam.usbhost_kbd_shell_command = def(seconds, addr, config, iface, ep)
    return fruitjam.usbhost_kbd_live_command("kbd-shell", seconds, addr, config, iface, ep)
end

fruitjam.usbhost_kbd_auto_command = def(mode, seconds)
    var cmd = "fruitjam-usbhost " + mode
    if seconds != nil
        cmd += " " + fruitjam.shell_quote(seconds)
    end
    return cmd
end

fruitjam.usbhost_kbd_auto_text_command = def(seconds)
    return fruitjam.usbhost_kbd_auto_command("kbd-auto-text", seconds)
end

fruitjam.usbhost_kbd_auto_events_command = def(seconds)
    return fruitjam.usbhost_kbd_auto_command("kbd-auto-events", seconds)
end

fruitjam.usbhost_kbd_auto_shell_command = def(seconds)
    return fruitjam.usbhost_kbd_auto_command("kbd-auto-shell", seconds)
end

fruitjam.usbhost_run_command = def(cmd)
    var status = os.system(cmd)
    return {"ok": status == 0, "status": status, "command": cmd}
end

fruitjam.usbhost_kbd_find = def()
    return fruitjam.usbhost_run_command(fruitjam.usbhost_kbd_find_command())
end

fruitjam.usbhost_kbd_text = def(seconds, addr, config, iface, ep)
    return fruitjam.usbhost_run_command(
        fruitjam.usbhost_kbd_text_command(seconds, addr, config, iface, ep))
end

fruitjam.usbhost_kbd_events = def(seconds, addr, config, iface, ep)
    return fruitjam.usbhost_run_command(
        fruitjam.usbhost_kbd_events_command(seconds, addr, config, iface, ep))
end

fruitjam.usbhost_kbd_shell = def(seconds, addr, config, iface, ep)
    return fruitjam.usbhost_run_command(
        fruitjam.usbhost_kbd_shell_command(seconds, addr, config, iface, ep))
end

fruitjam.hex2 = def(value)
    var digits = "0123456789abcdef"
    return digits[(value >> 4) & 15] + digits[value & 15]
end

fruitjam.hex_nibble = def(ch)
    if fruitjam.hex_values.contains(ch)
        return fruitjam.hex_values[ch]
    end
    return -1
end

fruitjam.hex_separator = def(ch)
    return ch == " " || ch == "\n" || ch == "\r" || ch == "\t" ||
           ch == ":" || ch == "," || ch == "-" || ch == "_"
end

fruitjam.hex_bytes = def(text, max_bytes)
    var out = []
    var hi = -1
    var i = 0
    while i < size(text)
        var ch = text[i]
        if fruitjam.hex_separator(ch) || ch == "x" || ch == "X"
            i += 1
        else
            var v = fruitjam.hex_nibble(ch)
            if v < 0
                return {"ok": false, "error": "bad hex"}
            end
            if hi < 0
                hi = v
            else
                if out.size() >= max_bytes
                    return {"ok": false, "error": "too much hex"}
                end
                out.push((hi << 4) | v)
                hi = -1
            end
            i += 1
        end
    end
    if hi >= 0
        return {"ok": false, "error": "odd hex"}
    end
    return {"ok": true, "bytes": out}
end

fruitjam.usb_pid_valid = def(pid)
    return (((pid >> 4) ^ (pid & 15)) == 15)
end

fruitjam.usb_pid_is_data = def(pid)
    return pid == 0xc3 || pid == 0x4b || pid == 0x87 || pid == 0x0f
end

fruitjam.hid_key_name = def(key)
    if key >= 4 && key <= 29
        return "abcdefghijklmnopqrstuvwxyz"[key - 4]
    end
    if key >= 30 && key <= 38
        return "123456789"[key - 30]
    end
    if key == 39
        return "0"
    end
    if fruitjam.hid_key_names.contains(key)
        return fruitjam.hid_key_names[key]
    end
    return "0x" + fruitjam.hex2(key)
end

fruitjam.hid_key_text_char = def(key, shift)
    if key >= 4 && key <= 29
        if shift
            return "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[key - 4]
        end
        return "abcdefghijklmnopqrstuvwxyz"[key - 4]
    end
    if key >= 30 && key <= 38
        if shift
            return "!@#$%^&*("[key - 30]
        end
        return "123456789"[key - 30]
    end
    if key == 39
        return shift ? ")" : "0"
    elif key == 44
        return " "
    elif key == 45
        return shift ? "_" : "-"
    elif key == 46
        return shift ? "+" : "="
    elif key == 47
        return shift ? "{" : "["
    elif key == 48
        return shift ? "}" : "]"
    elif key == 49
        return shift ? "|" : "\\"
    elif key == 51
        return shift ? ":" : ";"
    elif key == 52
        return shift ? "\"" : "'"
    elif key == 53
        return shift ? "~" : "`"
    elif key == 54
        return shift ? "<" : ","
    elif key == 55
        return shift ? ">" : "."
    elif key == 56
        return shift ? "?" : "/"
    end
    return ""
end

fruitjam.hid_boot_keyboard_report = def(report, source)
    if report.size() != 8 || report[1] != 0
        return {"ok": false, "source": source, "error": "not-boot-keyboard-report"}
    end

    var shift = (report[0] & (0x02 | 0x20)) != 0
    var keys = []
    var text = ""
    var i = 2
    while i < 8
        var key = report[i]
        if key >= 4
            var ch = fruitjam.hid_key_text_char(key, shift)
            var item = {
                "code": key,
                "hex": "0x" + fruitjam.hex2(key),
                "name": fruitjam.hid_key_name(key)
            }
            if ch != ""
                item["char"] = ch
                text += ch
            end
            keys.push(item)
        end
        i += 1
    end

    return {
        "ok": true,
        "source": source,
        "modifiers": report[0],
        "keys": keys,
        "text": text
    }
end

fruitjam.usbhost_hid_report = def(hex)
    var parsed = fruitjam.hex_bytes(hex, 32)
    if !parsed["ok"]
        return parsed
    end

    var bytes = parsed["bytes"]
    if bytes.size() == 0
        return {"ok": false, "source": "none", "error": "no-rx-data"}
    end

    var source = "raw-report"
    var report = bytes
    var pid_index = 0
    if bytes.size() >= 2 && fruitjam.usb_pid_valid(bytes[1])
        pid_index = 1
    end
    var pid = bytes[pid_index]

    if fruitjam.usb_pid_valid(pid) && fruitjam.usb_pid_is_data(pid)
        var payload_with_crc = bytes.size() - pid_index - 1
        if payload_with_crc < 2
            return {"ok": false, "source": "usb-data", "error": "data-packet-missing-crc"}
        end
        var report_len = payload_with_crc - 2
        report = []
        var i = 0
        while i < report_len
            report.push(bytes[pid_index + 1 + i])
            i += 1
        end
        source = "usb-data"
    end

    return fruitjam.hid_boot_keyboard_report(report, source)
end

fruitjam.adc_read = def(label, channel)
    var root = fruitjam.paths["adc"]
    try
        var raw = fruitjam.read_text(root + "/raw" + str(channel))
        var millivolts = fruitjam.read_text(root + "/millivolts" + str(channel))
        return {
            "ok": true,
            "label": label,
            "channel": channel,
            "raw": raw,
            "millivolts": millivolts
        }
    except .. as e, m
        return {
            "ok": false,
            "label": label,
            "channel": channel,
            "error": str(e) + ": " + str(m)
        }
    end
end

fruitjam.adc_summary = def()
    var out = []
    for item : fruitjam.adc_channels
        out.push(fruitjam.adc_read(item[0], item[1]))
    end
    return out
end

fruitjam.audio_clock = def(action)
    if action != "start" && action != "stop"
        return {"ok": false, "error": "bad audio action"}
    end
    try
        fruitjam.write_text(fruitjam.paths["audio"], action)
        return {"ok": true, "action": action}
    except .. as e, m
        return {"ok": false, "action": action, "error": str(e) + ": " + str(m)}
    end
end

fruitjam.audio_backend_args = def(backend)
    if backend == "beep"
        return " --beep"
    end
    if backend == "i2s"
        return " --i2s"
    end
    return ""
end

fruitjam.audio_loud_args = def(loud)
    if loud
        return " --loud"
    end
    return ""
end

fruitjam.audio_tone_command = def(hz, ms, loud, backend)
    var duration = ms
    if duration == nil || duration == ""
        duration = 1200
    end
    return "fruitjam-rtttl" + fruitjam.audio_backend_args(backend) +
           fruitjam.audio_loud_args(loud) + " --tone " +
           fruitjam.shell_quote(hz) + " " + fruitjam.shell_quote(duration)
end

fruitjam.rtttl_command = def(song, loud, backend)
    var cmd = "fruitjam-rtttl" + fruitjam.audio_backend_args(backend) +
              fruitjam.audio_loud_args(loud)
    if song != nil && song != ""
        cmd = cmd + " " + fruitjam.shell_quote(song)
    end
    return cmd
end

fruitjam.wav_analyze_command = def(path)
    return "fruitjam-wavplay --analyze " + fruitjam.shell_quote(path)
end

fruitjam.wav_play_command = def(path, backend, loud)
    return "fruitjam-wavplay" + fruitjam.audio_backend_args(backend) +
           fruitjam.audio_loud_args(loud) + " " + fruitjam.shell_quote(path)
end

fruitjam.audio_tone = def(hz, ms, loud, backend)
    var cmd = fruitjam.audio_tone_command(hz, ms, loud, backend)
    var status = os.system(cmd)
    return {"ok": status == 0, "status": status, "command": cmd}
end

fruitjam.rtttl = def(song, loud, backend)
    var cmd = fruitjam.rtttl_command(song, loud, backend)
    var status = os.system(cmd)
    return {"ok": status == 0, "status": status, "command": cmd}
end

fruitjam.wav_analyze = def(path)
    var cmd = fruitjam.wav_analyze_command(path)
    var status = os.system(cmd)
    return {"ok": status == 0, "status": status, "command": cmd}
end

fruitjam.wav_play = def(path, backend, loud)
    var cmd = fruitjam.wav_play_command(path, backend, loud)
    var status = os.system(cmd)
    return {"ok": status == 0, "status": status, "command": cmd}
end

fruitjam.dvi_command_allowed = def(command)
    for allowed : fruitjam.dvi_commands
        if command == allowed
            return true
        end
    end
    return false
end

fruitjam.dvi_command = def(command)
    if !fruitjam.dvi_command_allowed(command)
        return {"ok": false, "error": "bad dvi command"}
    end
    try
        fruitjam.write_text(fruitjam.paths["dvi"], command)
        return {"ok": true, "command": command}
    except .. as e, m
        return {"ok": false, "command": command, "error": str(e) + ": " + str(m)}
    end
end

fruitjam.mqtt_transport_flag = def(transport)
    if transport == "airlift"
        return " --airlift"
    end
    return ""
end

fruitjam.mqtt_auth_args = def(username, password)
    if username != nil && username != ""
        var pass = ""
        if password != nil
            pass = password
        end
        return " -u " + fruitjam.shell_quote(username) +
               " -P " + fruitjam.shell_quote(pass)
    end
    return ""
end

fruitjam.mqtt_pub_command = def(host, port, topic, message, client_id, username, password, transport)
    return "mosquitto_pub" + fruitjam.mqtt_transport_flag(transport) +
           " -h " + fruitjam.shell_quote(host) +
           " -p " + fruitjam.shell_quote(port) +
           " -i " + fruitjam.shell_quote(client_id) +
           fruitjam.mqtt_auth_args(username, password) +
           " -t " + fruitjam.shell_quote(topic) +
           " -m " + fruitjam.shell_quote(message)
end

fruitjam.mqtt_sub_command = def(host, port, topic, client_id, username, password, transport, count, wait, verbose)
    var cmd = "mosquitto_sub" + fruitjam.mqtt_transport_flag(transport) +
        " -h " + fruitjam.shell_quote(host) +
        " -p " + fruitjam.shell_quote(port) +
        " -i " + fruitjam.shell_quote(client_id) +
        fruitjam.mqtt_auth_args(username, password) +
        " -t " + fruitjam.shell_quote(topic)
    if count != nil && count > 0
        cmd += " -C " + fruitjam.shell_quote(count)
    end
    if wait != nil && wait > 0
        cmd += " -W " + fruitjam.shell_quote(wait)
    end
    if verbose
        cmd += " -v"
    end
    return cmd
end

fruitjam.mqtt_publish = def(host, port, topic, message, client_id, username, password, transport)
    var cmd = fruitjam.mqtt_pub_command(host, port, topic, message, client_id, username, password, transport)
    var status = os.system(cmd)
    return {"ok": status == 0, "status": status, "command": cmd}
end

fruitjam.mqtt_subscribe = def(host, port, topic, client_id, username, password, transport, count, wait, verbose)
    var cmd = fruitjam.mqtt_sub_command(host, port, topic, client_id, username, password, transport, count, wait, verbose)
    var status = os.system(cmd)
    return {"ok": status == 0, "status": status, "command": cmd}
end

fruitjam.mqtt_publish_script = def(path)
    var script = "#!/bin/sh\n" +
        "set -eu\n" +
        ": ${MQTT_HOST:=192.0.2.10}\n" +
        ": ${MQTT_PORT:=1883}\n" +
        ": ${MQTT_TOPIC:=charlie/test}\n" +
        ": ${MQTT_MESSAGE:=hello-from-berry}\n" +
        ": ${MQTT_CLIENT_ID:=fruitjam-berry-pub}\n" +
        ": ${MQTT_TRANSPORT:=airlift}\n" +
        "cmd=\"mosquitto_pub\"\n" +
        "if [ \"$MQTT_TRANSPORT\" = airlift ]; then cmd=\"$cmd --airlift\"; fi\n" +
        "if [ -n \"${MQTT_USER:-}\" ]; then\n" +
        "  exec $cmd -h \"$MQTT_HOST\" -p \"$MQTT_PORT\" -i \"$MQTT_CLIENT_ID\" -u \"$MQTT_USER\" -P \"${MQTT_PASSWORD:-}\" -t \"$MQTT_TOPIC\" -m \"$MQTT_MESSAGE\"\n" +
        "else\n" +
        "  exec $cmd -h \"$MQTT_HOST\" -p \"$MQTT_PORT\" -i \"$MQTT_CLIENT_ID\" -t \"$MQTT_TOPIC\" -m \"$MQTT_MESSAGE\"\n" +
        "fi\n"
    fruitjam.write_text(path, script)
    return {"ok": true, "path": path}
end

fruitjam.mqtt_subscribe_script = def(path)
    var script = "#!/bin/sh\n" +
        "set -eu\n" +
        ": ${MQTT_HOST:=192.0.2.10}\n" +
        ": ${MQTT_PORT:=1883}\n" +
        ": ${MQTT_TOPIC:=charlie/#}\n" +
        ": ${MQTT_CLIENT_ID:=fruitjam-berry-sub}\n" +
        ": ${MQTT_TRANSPORT:=airlift}\n" +
        ": ${MQTT_COUNT:=1}\n" +
        ": ${MQTT_WAIT:=30}\n" +
        "cmd=\"mosquitto_sub\"\n" +
        "if [ \"$MQTT_TRANSPORT\" = airlift ]; then cmd=\"$cmd --airlift\"; fi\n" +
        "if [ -n \"${MQTT_USER:-}\" ]; then\n" +
        "  exec $cmd -h \"$MQTT_HOST\" -p \"$MQTT_PORT\" -i \"$MQTT_CLIENT_ID\" -u \"$MQTT_USER\" -P \"${MQTT_PASSWORD:-}\" -t \"$MQTT_TOPIC\" -C \"$MQTT_COUNT\" -W \"$MQTT_WAIT\" -v\n" +
        "else\n" +
        "  exec $cmd -h \"$MQTT_HOST\" -p \"$MQTT_PORT\" -i \"$MQTT_CLIENT_ID\" -t \"$MQTT_TOPIC\" -C \"$MQTT_COUNT\" -W \"$MQTT_WAIT\" -v\n" +
        "fi\n"
    fruitjam.write_text(path, script)
    return {"ok": true, "path": path}
end

fruitjam.airlift_command = def(action)
    return "airliftctl " + action
end

fruitjam.airlift_tcp_get_command = def(host, path)
    var target = path
    if target == nil || target == ""
        target = "/"
    end
    return "airliftctl tcp-get " + fruitjam.shell_quote(host) +
           " " + fruitjam.shell_quote(target)
end

fruitjam.airlift_join_command = def(ssid, password)
    return "airliftctl join " + fruitjam.shell_quote(ssid) +
           " " + fruitjam.shell_quote(password)
end

fruitjam.airlift = def(action)
    var cmd = fruitjam.airlift_command(action)
    var status = os.system(cmd)
    return {"ok": status == 0, "status": status, "command": cmd}
end

fruitjam.airlift_tcp_get = def(host, path)
    var cmd = fruitjam.airlift_tcp_get_command(host, path)
    var status = os.system(cmd)
    return {"ok": status == 0, "status": status, "command": cmd}
end

fruitjam.i2c_addr_text = def(addr)
    if type(addr) == "string"
        return addr
    end
    return "0x" + fruitjam.hex2(addr)
end

fruitjam.i2c_scan_command = def(dev)
    var bus = dev
    if bus == nil || bus == ""
        bus = fruitjam.i2c_default_device
    end
    return "fruitjam-i2c scan " + fruitjam.shell_quote(bus)
end

fruitjam.i2c_ping_command = def(addr, dev)
    var bus = dev
    if bus == nil || bus == ""
        bus = fruitjam.i2c_default_device
    end
    return "fruitjam-i2c ping " + fruitjam.shell_quote(fruitjam.i2c_addr_text(addr)) +
           " " + fruitjam.shell_quote(bus)
end

fruitjam.i2c_scan = def(dev)
    var cmd = fruitjam.i2c_scan_command(dev)
    var status = os.system(cmd)
    return {"ok": status == 0, "status": status, "command": cmd}
end

fruitjam.i2c_ping = def(addr, dev)
    var cmd = fruitjam.i2c_ping_command(addr, dev)
    var status = os.system(cmd)
    return {"ok": status == 0, "status": status, "command": cmd}
end

fruitjam.neopixel_write = def(commands)
    var f = nil
    try
        f = open(fruitjam.paths["neopixels"], "r+")
    except .. as e, m
        return {"ok": false, "error": str(e) + ": " + str(m)}
    end
    for cmd : commands
        f.write(cmd + "\n")
    end
    f.flush()
    f.close()
    return {"ok": true}
end

fruitjam.neopixel_pixels = def(pixels)
    var commands = ["clear"]
    var i = 0
    for p : pixels
        var r = fruitjam.clamp(p[0], 0, 255)
        var g = fruitjam.clamp(p[1], 0, 255)
        var b = fruitjam.clamp(p[2], 0, 255)
        commands.push("set " + str(i) + " " + str(r) + " " + str(g) + " " + str(b))
        i += 1
    end
    commands.push("write")
    return fruitjam.neopixel_write(commands)
end

fruitjam.neopixel_fill = def(r, g, b)
    var pixels = []
    for i : 0 .. 4
        pixels.push([r, g, b])
    end
    return fruitjam.neopixel_pixels(pixels)
end

fruitjam.neopixel_clear = def()
    return fruitjam.neopixel_write(["clear", "write"])
end

return fruitjam
