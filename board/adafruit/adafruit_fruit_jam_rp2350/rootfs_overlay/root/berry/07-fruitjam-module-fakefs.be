import sys
import string

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

var i2c_scan = fruitjam.i2c_scan_command(nil)
var i2c_ping = fruitjam.i2c_ping_command(0x18, nil)
assert(string.find(i2c_scan, "fruitjam-i2c scan") == 0)
assert(string.find(i2c_ping, "fruitjam-i2c ping '0x18'") == 0)
assert(string.find(i2c_ping, "'/dev/i2c-0'") >= 0)
print("fake i2c helpers: ok")

assert(fruitjam.usbhost_kbd_find_command() == "fruitjam-usbhost kbd-find")
assert(fruitjam.usbhost_kbd_auto_text_command(1) == "fruitjam-usbhost kbd-auto-text '1'")
assert(fruitjam.usbhost_kbd_events_command(1, 1, 1, 0, 2) ==
       "fruitjam-usbhost kbd-events '1' '1' '1' '0' '2'")
assert(fruitjam.usbhost_kbd_shell_command(nil, 1, 1, 0, 2) ==
       "fruitjam-usbhost kbd-shell '0' '1' '1' '0' '2'")
print("fake usb keyboard helpers: ok")

assert(fruitjam.airlift_command("fw") == "airliftctl fw")
assert(fruitjam.airlift_tcp_get_command("example.com", "/") ==
       "airliftctl tcp-get 'example.com' '/'")
assert(fruitjam.airlift_join_command("ssid name", "pass word") ==
       "airliftctl join 'ssid name' 'pass word'")
print("fake airlift helpers: ok")

assert(fruitjam.audio_tone_command(880, 250, true, "beep") ==
       "fruitjam-rtttl --beep --loud --tone '880' '250'")
assert(fruitjam.rtttl_command("scale", false, "i2s") ==
       "fruitjam-rtttl --i2s 'scale'")
assert(fruitjam.wav_analyze_command("/mnt/sd/wavs/test.wav") ==
       "fruitjam-wavplay --analyze '/mnt/sd/wavs/test.wav'")
assert(fruitjam.wav_play_command("/mnt/sd/wavs/test.wav", "beep", true) ==
       "fruitjam-wavplay --beep --loud '/mnt/sd/wavs/test.wav'")
print("fake audio command helpers: ok")

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

assert(fruitjam.shell_quote("jam's") == "'jam'\\''s'")
var pub = fruitjam.mqtt_pub_command(
    "broker.local", 1883, "charlie/test", "hello jam",
    "fruitjam-berry-pub", "fred", "pass word", "airlift")
assert(string.find(pub, "mosquitto_pub --airlift") == 0)
assert(string.find(pub, "-u 'fred'") >= 0)
assert(string.find(pub, "-P 'pass word'") >= 0)
assert(string.find(pub, "-m 'hello jam'") >= 0)
var sub = fruitjam.mqtt_sub_command(
    "broker.local", 1883, "charlie/#", "fruitjam-berry-sub",
    "fred", "pass word", "airlift", 2, 10, true)
assert(string.find(sub, "mosquitto_sub --airlift") == 0)
assert(string.find(sub, "-C '2'") >= 0)
assert(string.find(sub, "-W '10'") >= 0)
assert(string.find(sub, "-v") >= 0)
print("fake mqtt helpers: ok")

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
