import sys

sys.path().push("/root/berry")
import fruitjam

print("Fruit Jam Berry USB HID decode")

var packet = fruitjam.usbhost_hid_report("f04b0000040000000000abcd")
assert(packet["ok"])
assert(packet["source"] == "usb-data")
assert(packet["modifiers"] == 0)
assert(packet["keys"].size() == 1)
assert(packet["keys"][0]["name"] == "a")
assert(packet["keys"][0]["hex"] == "0x04")
assert(packet["text"] == "a")
print("packet DATA1 a: ok")

var shifted = fruitjam.usbhost_hid_report("0200050000000000")
assert(shifted["ok"])
assert(shifted["source"] == "raw-report")
assert(shifted["modifiers"] == 2)
assert(shifted["keys"][0]["name"] == "b")
assert(shifted["text"] == "B")
print("raw shifted B: ok")

var desc = fruitjam.usbhost_hid_report("f04b12010002000000403412")
assert(!desc["ok"])
assert(desc["source"] == "usb-data")
assert(desc["error"] == "not-boot-keyboard-report")
print("descriptor rejection: ok")

print("08-usbhost-hid-decode.be: ok")
