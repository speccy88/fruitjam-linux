import sys
import string

sys.path().push("/root/berry")
import fruitjam

print("Fruit Jam Berry USB keyboard helper example")

assert(fruitjam.usbhost_kbd_find_command() == "fruitjam-usbhost kbd-find")
assert(fruitjam.usbhost_kbd_auto_text_command(1) == "fruitjam-usbhost kbd-auto-text '1'")
assert(fruitjam.usbhost_kbd_auto_events_command(1) == "fruitjam-usbhost kbd-auto-events '1'")
assert(fruitjam.usbhost_kbd_auto_shell_command(2) == "fruitjam-usbhost kbd-auto-shell '2'")

var text_cmd = fruitjam.usbhost_kbd_text_command(1, 1, 1, 0, 2)
assert(string.find(text_cmd, "fruitjam-usbhost kbd-text '1' '1' '1' '0' '2'") == 0)
var shell_cmd = fruitjam.usbhost_kbd_shell_command(nil, 1, 1, 0, 2)
assert(string.find(shell_cmd, "fruitjam-usbhost kbd-shell '0' '1' '1' '0' '2'") == 0)
print("command builders: ok")

if fruitjam.exists(fruitjam.paths["usbhost"])
    print("kernel USB-host bridge present; running kbd-find")
    var found = fruitjam.usbhost_kbd_find()
    if found["ok"]
        print("kbd-find: ok")
        print("live text smoke: " + fruitjam.usbhost_kbd_auto_text_command(10))
        print("live events smoke: " + fruitjam.usbhost_kbd_auto_events_command(10))
        print("keyboard shell: " + fruitjam.usbhost_kbd_auto_shell_command(nil))
    else
        print("kbd-find skipped/failed status=" + str(found["status"]))
    end
else
    print("usb keyboard skipped (/dev/fruitjam-usbhost missing)")
end

print("12-usbhost-keyboard.be: ok")
