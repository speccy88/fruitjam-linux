import time

var dev = "/dev/neopixels"
var colors = [
    ["red", 32, 0, 0],
    ["green", 0, 32, 0],
    ["blue", 0, 0, 32],
    ["yellow", 28, 20, 0],
    ["cyan", 0, 24, 24],
    ["purple", 18, 0, 28],
    ["white", 18, 18, 18]
]

def fill(r, g, b)
    var f = nil
    try
        f = open(dev, "r+")
    except .. as e, m
        print("neopixel-colors.be: skipped (" + str(e) + ": " + str(m) + ")")
        return false
    end
    f.write("clear\n")
    for i : 0 .. 4
        f.write("set " + str(i) + " " + str(r) + " " + str(g) + " " + str(b) + "\n")
    end
    f.write("write\n")
    f.flush()
    f.close()
    return true
end

var did_show = false

for c : colors
    print("neopixel-colors: " + c[0])
    if !fill(c[1], c[2], c[3])
        break
    end
    did_show = true
    time.sleep_ms(350)
end

if did_show
    fill(0, 0, 0)
end
print("neopixel-colors.be: ok")
