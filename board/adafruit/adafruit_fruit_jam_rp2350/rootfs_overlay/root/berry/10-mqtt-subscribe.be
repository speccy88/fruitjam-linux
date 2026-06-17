import sys
import string

sys.path().push("/root/berry")
import fruitjam

var out = "/tmp/fruitjam-mqtt-sub.sh"
var result = fruitjam.mqtt_subscribe_script(out)
assert(result["ok"])

var sample = fruitjam.mqtt_sub_command(
    "broker.local", 1883, "charlie/#", "fruitjam-berry-sub",
    "user", "", "airlift", 1, 30, true)
assert(string.find(sample, "mosquitto_sub --airlift") == 0)
assert(string.find(sample, "-t 'charlie/#'") >= 0)
assert(string.find(sample, "-C '1'") >= 0)
assert(string.find(sample, "-W '30'") >= 0)
assert(string.find(sample, "-v") >= 0)

print("Fruit Jam Berry MQTT subscribe example")
print("wrote " + out)
print("module command sample: " + sample)
print("run with broker settings, for example:")
print("MQTT_HOST=broker.local MQTT_USER=user MQTT_PASSWORD=... sh " + out)
print("10-mqtt-subscribe.be: ok")
