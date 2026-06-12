var buttons = [
    ["button1", 0],
    ["button2", 4],
    ["button3", 5]
]

import time

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
    print("03-buttons.be: skipped (gpio" + str(gpio) + " did not appear)")
    return false
end

def read_text(path)
    var f = open(path, "r")
    var s = f.read()
    f.close()
    return s
end

for button : buttons
    var name = button[0]
    var gpio = button[1]
    if !ensure_gpio(gpio)
        break
    end
    var value = read_text(gpio_value_path(gpio))
    var state = "pressed"
    if value[0] == "1"
        state = "released"
    end
    print(name + " gpio" + str(gpio) + " " + state)
end

print("03-buttons.be: ok")
