var BERRY_DIR = "/root/berry"

var examples = [
    BERRY_DIR + "/neopixel-colors.be",
    BERRY_DIR + "/neopixel-rainbow-10s.be"
]

def run(path)
    print("")
    print("== berry " + path + " ==")
    compile(path, "file")()
end

print("Fruit Jam Berry visual examples")

for path : examples
    run(path)
end

print("")
print("Fruit Jam Berry visual examples: ok")
