import sys
import string

sys.path().push("/root/berry")
import fruitjam

print("Fruit Jam Berry audio/WAV helper example")

assert(fruitjam.audio_tone_command(880, 250, true, "beep") ==
       "fruitjam-rtttl --beep --loud --tone '880' '250'")
assert(fruitjam.rtttl_command("scale", false, "i2s") ==
       "fruitjam-rtttl --i2s 'scale'")
assert(fruitjam.wav_analyze_command("/mnt/sd/wavs/test.wav") ==
       "fruitjam-wavplay --analyze '/mnt/sd/wavs/test.wav'")
assert(fruitjam.wav_play_command("/mnt/sd/wavs/test.wav", "beep", true) ==
       "fruitjam-wavplay --beep --loud '/mnt/sd/wavs/test.wav'")
print("command builders: ok")

if fruitjam.exists(fruitjam.paths["audio"])
    print("short tone smoke:")
    var tone = fruitjam.audio_tone(880, 200, false, "beep")
    if !tone["ok"]
        print("tone status=" + str(tone["status"]))
    end
else
    print("audio skipped (" + fruitjam.paths["audio"] + " missing)")
end

print("WAV analyze command: " +
      fruitjam.wav_analyze_command("/mnt/sd/wavs/fruitjam-scale.wav"))
print("WAV play command: " +
      fruitjam.wav_play_command("/mnt/sd/wavs/fruitjam-scale.wav", "i2s", true))

print("14-audio-wav.be: ok")
