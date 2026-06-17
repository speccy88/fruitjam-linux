import sys

sys.path().push("/root/berry")
import fruitjam

var FAKE_ROOT = "/tmp/fruitjam-berry-fakefs"

fruitjam.paths["gpio"] = FAKE_ROOT + "/gpio"
fruitjam.paths["adc"] = FAKE_ROOT + "/adc"
fruitjam.paths["neopixels"] = FAKE_ROOT + "/neopixels"
fruitjam.paths["audio"] = FAKE_ROOT + "/fruitjam-audio"
fruitjam.paths["dvi"] = FAKE_ROOT + "/fruitjam-dvi"
fruitjam.paths["i2c"] = FAKE_ROOT + "/i2c-0"
fruitjam.paths["spi"] = FAKE_ROOT + "/spidev0.0"
fruitjam.paths["usbhost"] = FAKE_ROOT + "/fruitjam-usbhost"

print("Fruit Jam Berry fake hardware module test")

var devices = fruitjam.device_status()
assert(devices["neopixels"])
assert(devices["audio"])
assert(devices["dvi"])
assert(devices["i2c"])
assert(devices["spi"])

var buttons = fruitjam.buttons_status()
assert(buttons.size() == 3)
assert(buttons[0]["state"] == "released")
assert(buttons[1]["state"] == "pressed")
assert(buttons[2]["state"] == "released")
print("fake buttons: ok")

var usb = fruitjam.usbhost_status()
assert(usb["power"] == 1)
assert(usb["dp"] == 1)
assert(usb["dm"] == 0)
assert(usb["device"] == "full-speed-device")
assert(usb["present"])
assert(!usb["hid"])
assert(usb["driver"] == "sysfs-line-state")
assert(usb["next"] == "pio-packet-io")
print("fake usbhost gpio: ok")

fruitjam.write_text(fruitjam.paths["usbhost"],
    "power 1\n" +
    "dp 1\n" +
    "dm 0\n" +
    "device full-speed-device\n" +
    "present 1\n" +
    "hid 0\n" +
    "driver kernel-line-state\n" +
    "bridge pio-packet-io-program-loaded\n" +
    "pio 2\n" +
    "pio_ready 1\n" +
    "pio_configured 1\n" +
    "pio_program pico-pio-usb-fs-host-32word\n" +
    "packets 12\n" +
    "tx_errors 0\n" +
    "last_tx_result 0\n" +
    "last_tx_len 4\n" +
    "rx_attempts 5\n" +
    "rx_errors 0\n" +
    "last_rx_result 0\n" +
    "last_rx_len 11\n" +
    "last_rx_pid 0x4b\n" +
    "last_rx_hex f04b0000040000000000abcd\n" +
    "probe_summary kbd-poll addr=1 ep=2 len=11 pid=0x4b\n" +
    "first_milestone boot-protocol-keyboard\n")
usb = fruitjam.usbhost_status()
assert(usb["driver"] == "kernel-line-state")
assert(usb["bridge"] == "pio-packet-io-program-loaded")
assert(usb["pio_ready"])
assert(usb["pio_configured"])
assert(usb["pio"] == 2)
assert(usb["packets"] == 12)
assert(usb["last_rx_pid"] == 0x4b)
assert(usb["last_rx_hex"] == "f04b0000040000000000abcd")
assert(usb["probe_summary"] == "kbd-poll addr=1 ep=2 len=11 pid=0x4b")
print("fake usbhost bridge: ok")

var adc = fruitjam.adc_summary()
assert(adc.size() == 3)
assert(adc[0]["ok"])
assert(adc[0]["raw"] == "123\n")
assert(adc[1]["millivolts"] == "1650\n")
assert(adc[2]["label"] == "temperature")
print("fake adc: ok")

assert(fruitjam.audio_clock("start")["ok"])
assert(fruitjam.read_text(fruitjam.paths["audio"]) == "start")
assert(fruitjam.audio_clock("stop")["ok"])
assert(fruitjam.read_text(fruitjam.paths["audio"]) == "stop")
assert(!fruitjam.audio_clock("bad")["ok"])
print("fake audio: ok")

assert(fruitjam.dvi_command("show")["ok"])
assert(fruitjam.read_text(fruitjam.paths["dvi"]) == "show")
assert(!fruitjam.dvi_command("exec fruitjam-services status")["ok"])
print("fake dvi: ok")

var neo = fruitjam.neopixel_pixels([
    [-1, 0, 999],
    [1, 2, 3],
    [4, 5, 6],
    [7, 8, 9],
    [10, 11, 12]
])
assert(neo["ok"])
var neo_text = fruitjam.read_text(fruitjam.paths["neopixels"])
assert(neo_text == "clear\nset 0 0 0 255\nset 1 1 2 3\nset 2 4 5 6\nset 3 7 8 9\nset 4 10 11 12\nwrite\n")
fruitjam.write_text(fruitjam.paths["neopixels"], "")
assert(fruitjam.neopixel_clear()["ok"])
assert(fruitjam.read_text(fruitjam.paths["neopixels"]) == "clear\nwrite\n")
print("fake neopixels: ok")

print("07-fruitjam-module-fakefs.be: ok")
