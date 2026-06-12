import string
import json
import math
import time
import sys

sys.path().push("/root/berry")
import fruitjam_lib

print("Fruit Jam Berry language tour")

var total = 0
for i : 1 .. 5
    total += i
end
assert(total == 15)

var n = 0
while n < 3
    n += 1
end
assert(n == 3)

var numbers = [3, 1, 4]
numbers.push(1)
numbers .. 5
assert(numbers.size() == 5)
assert(numbers[0] == 3)

var labels = {
    "button1": "GPIO0",
    "button2": "GPIO4",
    "button3": "GPIO5"
}
labels["neopixels"] = "/dev/neopixels"
assert(labels.contains("button2"))
assert(labels.find("missing", "none") == "none")

def make_counter(start)
    var value = start
    return def(step)
        value += step
        return value
    end
end

var counter = make_counter(10)
assert(counter(5) == 15)
assert(counter(-3) == 12)

class Blinker
    var label
    var count
    def init(label, count)
        self.label = label
        self.count = count
    end
    def summary()
        return self.label + ":" + str(self.count)
    end
    def iter()
        var i = 0, limit = self.count
        return def()
            if i >= limit
                raise "stop_iteration"
            end
            i += 1
            return i
        end
    end
end

var b = Blinker("neo", 3)
assert(b.summary() == "neo:3")
var iter_sum = 0
for step : b
    iter_sum += step
end
assert(iter_sum == 6)

var add3 = /a b c-> a + b + c
assert(add3(1, 2, 3) == 6)

assert(string.toupper("fruit jam") == "FRUIT JAM")
var decoded = json.load('{"board":"fruitjam","leds":5}')
assert(decoded["board"] == "fruitjam")
assert(decoded["leds"] == 5)
assert(json.dump({"ok": true}) == '{"ok":true}')
assert(math.max(2, 7, 3) == 7)

var now = time.time()
assert(type(now) == "int")
var now_fields = time.dump(now)
assert(now_fields.contains("year"))
assert(now_fields.contains("sec"))

assert(fruitjam_lib.clamp(12, 0, 5) == 5)
assert(fruitjam_lib.clamp(-3, 0, 5) == 0)
assert(fruitjam_lib.join_label("A", 0) == "A0")
var module_counter = fruitjam_lib.Counter(2)
assert(module_counter.step(4) == 6)

print("loops/maps/functions/classes/modules/json/math/time: ok")
