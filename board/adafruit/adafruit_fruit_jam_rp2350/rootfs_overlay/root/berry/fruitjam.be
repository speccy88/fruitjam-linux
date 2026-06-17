var fruitjam = module("fruitjam")

import time

fruitjam.paths = {
    "neopixels": "/dev/neopixels",
    "audio": "/dev/fruitjam-audio",
    "dvi": "/dev/fruitjam-dvi",
    "i2c": "/dev/i2c-0",
    "spi": "/dev/spidev0.0",
    "gpio": "/sys/class/gpio",
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

fruitjam.device_status = def()
    return {
        "neopixels": fruitjam.exists(fruitjam.paths["neopixels"]),
        "audio": fruitjam.exists(fruitjam.paths["audio"]),
        "dvi": fruitjam.exists(fruitjam.paths["dvi"]),
        "i2c": fruitjam.exists(fruitjam.paths["i2c"]),
        "spi": fruitjam.exists(fruitjam.paths["spi"])
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

fruitjam.usbhost_status = def()
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
