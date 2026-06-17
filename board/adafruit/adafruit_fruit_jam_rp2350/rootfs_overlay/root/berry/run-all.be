var BERRY_DIR = "/root/berry"

var examples = [
    BERRY_DIR + "/00-hello.be",
    BERRY_DIR + "/01-language-tour.be",
    BERRY_DIR + "/02-files-and-sd.be",
    BERRY_DIR + "/03-buttons.be",
    BERRY_DIR + "/04-adc-summary.be",
    BERRY_DIR + "/05-usbhost-status.be",
    BERRY_DIR + "/06-fruitjam-module.be",
    BERRY_DIR + "/08-usbhost-hid-decode.be",
    BERRY_DIR + "/neopixels.be",
    BERRY_DIR + "/neopixel-colors.be"
]

def run(path)
    print("")
    print("== berry " + path + " ==")
    compile(path, "file")()
end

print("Fruit Jam Berry example runner")

for path : examples
    run(path)
end

print("")
print("Fruit Jam Berry examples: ok")
print("Fruit Jam Berry examples: all tests passed")
