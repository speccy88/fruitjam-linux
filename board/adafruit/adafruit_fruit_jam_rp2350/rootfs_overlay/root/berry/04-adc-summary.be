var adc_root = "/sys/bus/platform/devices/400a0000.adc"

def read_attr(name)
    var path = adc_root + "/" + name
    var f = open(path, "r")
    var value = f.read()
    f.close()
    return value
end

def show_channel(label, channel)
    var raw = read_attr("raw" + str(channel))
    var mv = read_attr("millivolts" + str(channel))
    print(label + " adc" + str(channel) + " raw " + raw + "millivolts " + mv)
end

try
    show_channel("A0/GPIO40", 0)
    show_channel("A1/GPIO41", 1)
    show_channel("temperature", 8)
except .. as e, m
    print("04-adc-summary.be: skipped (" + str(e) + ": " + str(m) + ")")
end

print("04-adc-summary.be: ok")
