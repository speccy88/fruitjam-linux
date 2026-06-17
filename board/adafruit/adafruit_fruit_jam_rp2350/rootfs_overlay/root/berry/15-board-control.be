import sys
import string

sys.path().push("/root/berry")
import fruitjam

print("Fruit Jam Berry board-control helper example")

assert(fruitjam.board_status_command() == "fruitjamctl status")
assert(fruitjam.board_buttons_command() == "fruitjamctl buttons")
assert(fruitjam.led_command("toggle") == "fruitjamctl led 'toggle'")
assert(fruitjam.usb_power_command("on") == "fruitjamctl usb-power 'on'")
assert(fruitjam.periph_reset_command("pulse") == "fruitjamctl periph-reset 'pulse'")
assert(fruitjam.bootsel_command(nil) == "fruitjamctl bootsel")
assert(fruitjam.bootsel_command(1200) == "fruitjamctl bootsel '1200'")
assert(fruitjam.led_command("blink") == "")
assert(fruitjam.usb_power_command("maybe") == "")
assert(fruitjam.periph_reset_command("bad") == "")
print("command builders: ok")

if fruitjam.exists("/usr/bin/fruitjamctl")
    print("board status:")
    var status = fruitjam.board_status()
    if !status["ok"]
        print("status exit=" + str(status["status"]))
    end
else
    print("fruitjamctl status skipped (/usr/bin/fruitjamctl missing)")
end

print("safe control commands:")
print("  " + fruitjam.led_command("on"))
print("  " + fruitjam.led_command("off"))
print("  " + fruitjam.usb_power_command("on"))
print("  " + fruitjam.periph_reset_command("pulse"))
print("  " + fruitjam.bootsel_command(1200))

print("15-board-control.be: ok")
