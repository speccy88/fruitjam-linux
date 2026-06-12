Fruit Jam shell examples
========================

Run one script:

  sh /root/sh/00-board-info.sh

Run the finite shell hardware test set:

  sh /root/sh/run-all.sh

Files:

  00-board-info.sh         Kernel, uptime, mounts, and key device paths.
  01-services-http.sh      Service status plus loopback CGI through wget.
  02-neopixels-pattern.sh  Direct /dev/neopixels static pattern.
  03-neopixels-chase.sh    Direct /dev/neopixels chase animation.
  04-buttons-read.sh       fruitjamctl, fruitjam-buttons, and sysfs button reads.
  05-i2c-scan.sh           Fast TLV320DAC3100 I2C ping at 0x18.
  06-adc-read.sh           fruitjam-adc reads for A0/A1/temperature.
  07-sd-card.sh            microSD write/read/remove check.
  08-audio-clock.sh        Start/stop the Fruit Jam audio clock helper.
  09-wget-loopback.sh      target-side wget against local httpd.
  10-berry-smoke.sh        Berry example smoke set.
  11-file-remove.sh        Create/remove a temp file with standalone rm.
  12-dvi-dashboard.sh      Render a compact dashboard to /dev/fruitjam-dvi.
  13-dvi-command.sh        Render command output, by default service status, to DVI.
  14-usbhost-status.sh     Report USB host power and D+/D- line status.
  15-wav-analyze.sh        Analyze the first SD WAV file without playing audio.
  serial-over-tcp.sh       Long-running TCP bridge for a character device.
  run-all.sh               Runs finite shell hardware checks; RTTTL is direct.

RTTTL note:

Run tunes directly, for example `fruitjam-rtttl scale`. Avoid wrapping RTTTL
playback inside larger shell scripts on this no-MMU image.
