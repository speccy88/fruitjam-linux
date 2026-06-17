var out = "/tmp/fruitjam-mqtt-sub.sh"

var script = "#!/bin/sh\n" +
    "set -eu\n" +
    ": ${MQTT_HOST:=192.0.2.10}\n" +
    ": ${MQTT_PORT:=1883}\n" +
    ": ${MQTT_TOPIC:=charlie/#}\n" +
    ": ${MQTT_CLIENT_ID:=fruitjam-berry-sub}\n" +
    ": ${MQTT_TRANSPORT:=airlift}\n" +
    ": ${MQTT_COUNT:=1}\n" +
    ": ${MQTT_WAIT:=30}\n" +
    "cmd=\"mosquitto_sub\"\n" +
    "if [ \"$MQTT_TRANSPORT\" = airlift ]; then cmd=\"$cmd --airlift\"; fi\n" +
    "if [ -n \"${MQTT_USER:-}\" ]; then\n" +
    "  exec $cmd -h \"$MQTT_HOST\" -p \"$MQTT_PORT\" -i \"$MQTT_CLIENT_ID\" -u \"$MQTT_USER\" -P \"${MQTT_PASSWORD:-}\" -t \"$MQTT_TOPIC\" -C \"$MQTT_COUNT\" -W \"$MQTT_WAIT\" -v\n" +
    "else\n" +
    "  exec $cmd -h \"$MQTT_HOST\" -p \"$MQTT_PORT\" -i \"$MQTT_CLIENT_ID\" -t \"$MQTT_TOPIC\" -C \"$MQTT_COUNT\" -W \"$MQTT_WAIT\" -v\n" +
    "fi\n"

var f = open(out, "w")
f.write(script)
f.close()

print("Fruit Jam Berry MQTT subscribe example")
print("wrote " + out)
print("run with broker settings, for example:")
print("MQTT_HOST=broker.local MQTT_USER=user MQTT_PASSWORD=... sh " + out)
print("10-mqtt-subscribe.be: ok")
