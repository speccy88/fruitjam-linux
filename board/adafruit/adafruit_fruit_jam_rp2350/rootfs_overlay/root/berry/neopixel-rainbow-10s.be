import time

var dev = "/dev/neopixels"
var palette = [
    [36, 0, 0],
    [30, 12, 0],
    [22, 24, 0],
    [0, 32, 0],
    [0, 24, 24],
    [0, 0, 36],
    [18, 0, 30],
    [32, 0, 12]
]

def show_frame(step)
    var f = nil
    try
        f = open(dev, "r+")
    except .. as e, m
        print("neopixel-rainbow-10s.be: skipped (" + str(e) + ": " + str(m) + ")")
        return false
    end
    f.write("clear\n")
    for i : 0 .. 4
        var color = palette[(step + i) % palette.size()]
        f.write("set " + str(i) + " " + str(color[0]) + " " + str(color[1]) + " " + str(color[2]) + "\n")
    end
    f.write("write\n")
    f.flush()
    f.close()
    return true
end

print("neopixel-rainbow-10s: start")

for step : 0 .. 49
    if !show_frame(step)
        break
    end
    time.sleep_ms(200)
end

try
    var f = open(dev, "r+")
    f.write("clear\nwrite\n")
    f.flush()
    f.close()
except .. as e, m
end

print("neopixel-rainbow-10s.be: ok")
