import time

var gpios = {
    "power": 11,
    "dp": 1,
    "dm": 2
}

def pause_ms(ms)
    try
        time.sleep_ms(ms)
    except .. as e, m
        var start = time.clock()
        while (time.clock() - start) < (ms / 1000.0)
        end
    end
end

def gpio_value_path(gpio)
    return "/sys/class/gpio/gpio" + str(gpio) + "/value"
end

def gpio_direction_path(gpio)
    return "/sys/class/gpio/gpio" + str(gpio) + "/direction"
end

def file_exists(path)
    try
        var f = open(path, "r")
        f.close()
        return true
    except .. as e, m
        return false
    end
end

def ensure_gpio(gpio)
    var path = gpio_value_path(gpio)
    if file_exists(path)
        return true
    end
    try
        var ex = open("/sys/class/gpio/export", "w")
        ex.write(str(gpio) + "\n")
        ex.close()
    except .. as e, m
    end
    for attempt : 0 .. 49
        if file_exists(path)
            return true
        end
        pause_ms(2)
    end
    return false
end

def write_text(path, text)
    var f = open(path, "w")
    f.write(text)
    f.close()
end

def read_gpio(gpio, direction)
    if !ensure_gpio(gpio)
        return -1
    end
    if direction != nil
        try
            write_text(gpio_direction_path(gpio), direction)
        except .. as e, m
            return -1
        end
    end
    var f = open(gpio_value_path(gpio), "r")
    var value = f.read()
    f.close()
    if value[0] == "0"
        return 0
    end
    return 1
end

var power = read_gpio(gpios["power"], nil)
var dp = read_gpio(gpios["dp"], "in")
var dm = read_gpio(gpios["dm"], "in")
var device = "unknown"

if power < 0
    device = "unknown"
elif power == 0
    device = "power-off"
elif dp == 0 && dm == 0
    device = "no-device-or-reset"
elif dp == 1 && dm == 0
    device = "full-speed-device"
elif dp == 0 && dm == 1
    device = "low-speed-device"
elif dp == 1 && dm == 1
    device = "invalid-both-lines-high"
end

print("usbhost power gpio11=" + str(power))
print("usbhost dp gpio1=" + str(dp) + " dm gpio2=" + str(dm))
print("usbhost device " + device)
print("usbhost hid-ready sysfs-line-state; next pio-packet-io")
print("05-usbhost-status.be: ok")
