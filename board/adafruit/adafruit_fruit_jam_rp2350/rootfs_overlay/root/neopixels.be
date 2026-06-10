var dev = "/dev/neopixels"

def send(f, cmd)
    print("> " + cmd)
    f.write(cmd + "\n")
    f.flush()
end

def main()
    var f = nil

    try
        f = open(dev, "r+")
    except .. as e, v
        print("neopixels.be: cannot open " + dev)
        print("neopixels.be: expected commands: clear, set INDEX R G B, write")
        print("neopixels.be: add the NeoPixel device/control driver, then run this again.")
        return
    end

    send(f, "clear")
    send(f, "set 0 24 0 0")
    send(f, "set 1 24 12 0")
    send(f, "set 2 0 24 0")
    send(f, "set 3 0 0 24")
    send(f, "set 4 12 0 24")
    send(f, "write")
    f.close()

    print("neopixels.be: five-pixel test pattern sent")
end

main()
