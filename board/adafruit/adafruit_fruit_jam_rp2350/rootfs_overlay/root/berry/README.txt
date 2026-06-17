Fruit Jam Berry examples
========================

Run one script:

  berry /root/berry/00-hello.be

Run the full finite Berry test set:

  berry-run /root/berry/run-all.be

Run the visual NeoPixel set:

  berry-run /root/berry/run-visual.be

Run your own Berry files from the web playground:

  mkdir -p /mnt/sd/berry
  cp my-script.be /mnt/sd/berry/
  open http://<fruit-jam-ip>/playground

The playground lists built-in examples plus regular `.be` files directly under
/mnt/sd/berry. SD-card files are shown as `SD: name.be` and are run through the
same tiny JSON helper as the built-in examples.

Files:

  00-hello.be              Tiny Berry sanity check.
  01-language-tour.be      Loops, maps, lists, functions, classes, modules, JSON, math, time.
  02-files-and-sd.be       Built-in open/read/write checks for /tmp and microSD.
  03-buttons.be            Direct sysfs button reads for GPIO0/GPIO4/GPIO5.
  04-adc-summary.be        Direct sysfs ADC reads for A0/A1 and temperature.
  05-usbhost-status.be     Direct sysfs USB host power/D+/D- status classifier.
  06-fruitjam-module.be    Smoke test for the importable fruitjam hardware module.
  07-fruitjam-module-fakefs.be
                            Host validation fixture for fake GPIO/ADC/audio/DVI/NeoPixel paths.
  08-usbhost-hid-decode.be
                            Berry decode checks for raw and DATA0/DATA1 HID keyboard reports.
  09-mqtt-publish.be        Uses fruitjam MQTT helpers and writes /tmp/fruitjam-mqtt-pub.sh.
  10-mqtt-subscribe.be      Uses fruitjam MQTT helpers and writes /tmp/fruitjam-mqtt-sub.sh.
  11-i2c.be                 Uses fruitjam I2C helpers to scan and ping TLV320DAC3100.
  12-usbhost-keyboard.be    Uses fruitjam USB keyboard helpers for kbd-find and live smoke commands.
  fruitjam.be              Importable helpers for GPIO, buttons, ADC, USB-host status,
                            HID decode, USB keyboard command build/run, I2C scan/ping,
                            MQTT command build/run, audio clock, DVI command writes,
                            device presence, and NeoPixels. USB-host status prefers the
                            kernel bridge and falls back to GPIO line-state reads.
  fruitjam_lib.be          Small importable Berry module used by the language tour.
  neopixels.be             Five-pixel static smoke pattern.
  neopixel-colors.be       RGB color cycle inspired by the Fruit Jam NeoPixel examples.
  neopixel-rainbow-10s.be  About 10 seconds of animated onboard NeoPixels.
  run-all.be               Runs the finite Berry example set in one VM.
  run-visual.be            Runs the longer NeoPixel visual examples.

Use berry-run for multi-script examples. It is a tiny C launcher that clears
page cache before execing Berry, which helps on this no-MMU target after network
or SD tests have warmed the cache.
