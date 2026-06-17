import sys

sys.path().push("/root/berry")
import fruitjam

print("Fruit Jam Berry hardware module")

assert(fruitjam.clamp(-5, 0, 10) == 0)
assert(fruitjam.clamp(5, 0, 10) == 5)
assert(fruitjam.clamp(15, 0, 10) == 10)

var devices = fruitjam.device_status()
print("devices neopixels=" + str(devices["neopixels"]) +
      " audio=" + str(devices["audio"]) +
      " dvi=" + str(devices["dvi"]) +
      " i2c=" + str(devices["i2c"]) +
      " spi=" + str(devices["spi"]))

var buttons = fruitjam.buttons_status()
assert(buttons.size() == 3)
for button : buttons
    print(button["name"] + " gpio" + str(button["gpio"]) + " " + button["state"])
end

var usb = fruitjam.usbhost_status()
assert(usb.contains("device"))
print("usbhost device " + usb["device"])

var adc = fruitjam.adc_summary()
assert(adc.size() == 3)
for item : adc
    if item["ok"]
        print(item["label"] + " raw " + item["raw"] + "millivolts " + item["millivolts"])
    else
        print(item["label"] + " skipped (" + item["error"] + ")")
    end
end

var audio_start = fruitjam.audio_clock("start")
if audio_start["ok"]
    print("fruitjam.audio_clock start: ok")
    fruitjam.audio_clock("stop")
else
    print("fruitjam.audio_clock start: skipped (" + audio_start["error"] + ")")
end

var dvi = fruitjam.dvi_command("show")
if dvi["ok"]
    print("fruitjam.dvi_command show: ok")
else
    print("fruitjam.dvi_command show: skipped (" + dvi["error"] + ")")
end

var neo = fruitjam.neopixel_pixels([
    [24, 0, 0],
    [24, 12, 0],
    [0, 24, 0],
    [0, 0, 24],
    [12, 0, 24]
])
if neo["ok"]
    print("fruitjam.neopixel_pixels: ok")
    fruitjam.neopixel_clear()
else
    print("fruitjam.neopixel_pixels: skipped (" + neo["error"] + ")")
end

print("06-fruitjam-module.be: ok")
