def write_read(path, text)
    var f = open(path, "w")
    f.write(text)
    f.close()

    f = open(path, "r")
    var got = f.read()
    f.close()

    assert(got == text)
    print(path + ": ok")
end

var text = "fruitjam berry file io ok\n"

write_read("/tmp/berry-file-test.txt", text)

try
    write_read("/mnt/sd/berry-sd-test.txt", text)
except .. as e, m
    print("/mnt/sd: skipped (" + str(e) + ": " + str(m) + ")")
end

print("02-files-and-sd.be: ok")
