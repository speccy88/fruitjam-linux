import sys
import string

sys.path().push("/root/berry")
import fruitjam

var out = "/tmp/fruitjam-mqtt-pub.sh"
var result = fruitjam.mqtt_publish_script(out)
assert(result["ok"])

var sample = fruitjam.mqtt_pub_command(
    "broker.local", 1883, "charlie/test", "hello-from-berry",
    "fruitjam-berry-pub", "user", "", "airlift")
assert(string.find(sample, "mosquitto_pub --airlift") == 0)
assert(string.find(sample, "-t 'charlie/test'") >= 0)
assert(string.find(sample, "-m 'hello-from-berry'") >= 0)

print("Fruit Jam Berry MQTT publish example")
print("wrote " + out)
print("module command sample: " + sample)
print("run with broker settings, for example:")
print("MQTT_HOST=broker.local MQTT_USER=user MQTT_PASSWORD=... sh " + out)
print("09-mqtt-publish.be: ok")
