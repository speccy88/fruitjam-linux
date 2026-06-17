import sys
import string

sys.path().push("/root/berry")
import fruitjam

print("Fruit Jam Berry AirLift helper example")

assert(fruitjam.airlift_command("fw") == "airliftctl fw")
assert(fruitjam.airlift_command("status") == "airliftctl status")
assert(fruitjam.airlift_tcp_get_command("example.com", "/") ==
       "airliftctl tcp-get 'example.com' '/'")
assert(string.find(fruitjam.airlift_join_command("ssid name", "pass word"),
                   "airliftctl join 'ssid name' 'pass word'") == 0)
print("command builders: ok")

if fruitjam.exists(fruitjam.paths["spi"])
    print("AirLift SPI device present; running cached/direct diagnostics")
    for action : ["fw", "mac", "status", "ip"]
        print("airlift " + action + ":")
        var result = fruitjam.airlift(action)
        if !result["ok"]
            print("airlift " + action + " status=" + str(result["status"]))
        end
    end
    print("TCP smoke command: " + fruitjam.airlift_tcp_get_command("example.com", "/"))
else
    print("airlift skipped (" + fruitjam.paths["spi"] + " missing)")
end

print("13-airlift.be: ok")
