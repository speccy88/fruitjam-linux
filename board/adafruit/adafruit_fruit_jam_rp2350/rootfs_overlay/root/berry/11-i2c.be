import sys
import string

sys.path().push("/root/berry")
import fruitjam

print("Fruit Jam Berry I2C example")

var scan_cmd = fruitjam.i2c_scan_command(nil)
var ping_cmd = fruitjam.i2c_ping_command(0x18, nil)
assert(string.find(scan_cmd, "fruitjam-i2c scan") == 0)
assert(string.find(ping_cmd, "fruitjam-i2c ping '0x18'") == 0)

if fruitjam.exists(fruitjam.paths["i2c"])
    print("i2c scan:")
    var scan = fruitjam.i2c_scan(nil)
    assert(scan["ok"])
    print("i2c ping TLV320DAC3100 0x18:")
    var ping = fruitjam.i2c_ping(0x18, nil)
    assert(ping["ok"])
else
    print("i2c skipped (" + fruitjam.paths["i2c"] + " missing)")
end

print("11-i2c.be: ok")
