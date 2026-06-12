Fruit Jam RTTTL examples
========================

The Fruit Jam image plays simple RTTTL tones through the TLV320DAC3100 codec.
The /dev/fruitjam-audio helper supplies the codec clock and tiny I2S tone
stream used by the C player:

  fruitjam-rtttl scale
  fruitjam-rtttl --loud --tone 880 2500

Other built-in examples:

  fruitjam-rtttl startup
  fruitjam-rtttl retro
  fruitjam-rtttl chime

You can list built-ins and still play the RTTTL files directly:

  fruitjam-rtttl --list
  fruitjam-rtttl /root/rtttl/01-scale.rtttl

The direct `fruitjam-rtttl` form uses the least memory. Convenience wrappers are
also provided for quick tests and immediately exec the C player:

  sh /root/rtttl/01-scale.sh
  sh /root/rtttl/02-startup.sh
  sh /root/rtttl/03-retro-game.sh
  sh /root/rtttl/04-soft-chime.sh

Run tunes one at a time after the shell prompt returns.

The codec beep generator can still be tested explicitly with
`fruitjam-rtttl --beep`, but the normal path is I2S.
