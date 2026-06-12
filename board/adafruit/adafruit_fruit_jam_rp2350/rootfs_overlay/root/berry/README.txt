Fruit Jam Berry examples
========================

Run one script:

  berry /root/berry/00-hello.be

Run the full finite Berry test set:

  berry-run /root/berry/run-all.be

Run the visual NeoPixel set:

  berry-run /root/berry/run-visual.be

Files:

  00-hello.be              Tiny Berry sanity check.
  01-language-tour.be      Loops, maps, lists, functions, classes, modules, JSON, math, time.
  02-files-and-sd.be       Built-in open/read/write checks for /tmp and microSD.
  03-buttons.be            Direct sysfs button reads for GPIO0/GPIO4/GPIO5.
  04-adc-summary.be        Direct sysfs ADC reads for A0/A1 and temperature.
  05-usbhost-status.be     Direct sysfs USB host power/D+/D- status classifier.
  fruitjam_lib.be          Small importable Berry module used by the language tour.
  neopixels.be             Five-pixel static smoke pattern.
  neopixel-colors.be       RGB color cycle inspired by the Fruit Jam NeoPixel examples.
  neopixel-rainbow-10s.be  About 10 seconds of animated onboard NeoPixels.
  run-all.be               Runs the finite Berry example set in one VM.
  run-visual.be            Runs the longer NeoPixel visual examples.

Use berry-run for multi-script examples. It is a tiny C launcher that clears
page cache before execing Berry, which helps on this no-MMU target after network
or SD tests have warmed the cache.
