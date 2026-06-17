# Adafruit Fruit Jam RP2350B pin map

This document records board wiring relevant to the no-MMU Linux port. The current
image uses PSRAM, flash, UART, USB CDC, sysfs GPIO, AirLift userspace SPI, and the
onboard NeoPixels; the remaining entries are planning notes for later milestones.

| Function | Fruit Jam / RP2350B GPIO | Linux milestone status | Notes |
| --- | ---: | --- | --- |
| QSPI flash | Dedicated RP2350 QSPI pins | Milestone A used by boot ROM/XIP | 16 MiB flash, represented as `phram` at `0x10000000` in the DT. |
| PSRAM CS | GPIO47 | Milestone A bootloader | QMI/XIP CS1. Confirmed from Adafruit CircuitPython Fruit Jam board definition; recheck the Eagle schematic net if respinning. |
| PSRAM data/clock | Dedicated QMI/XIP pins | Milestone A bootloader | 8 MiB external PSRAM mapped at `0x11000000`. |
| UART console TX | GPIO8 | Milestone A | Fruit Jam labeled `TX`, RP2350 UART1 TX. |
| UART console RX | GPIO9 | Milestone A | Fruit Jam labeled `RX`, RP2350 UART1 RX. |
| Red LED / IR | GPIO29 | Bootloader + sysfs GPIO | Active-low LED; raw sysfs value `0` turns it on, raw value `1` turns it off. Shared with IR receiver. |
| Button 1 / BOOT | GPIO0 | Sysfs GPIO input | Active-low button with pull-up. Released reads `1`; holding at reset enters ROM boot. |
| Button 2 | GPIO4 | Sysfs GPIO input | Active-low button with pull-up. Released reads `1`. |
| Button 3 | GPIO5 | Sysfs GPIO input | Active-low button with pull-up. Released reads `1`. |
| NeoPixels | GPIO32 | `/dev/neopixels` | Five onboard WS2812-style status NeoPixels, driven by a PIO-backed misc driver. |
| microSD SCK | GPIO34 | SPI/MMC block device | RP2350 PL022 SPI0 clock; also SDIO clock. |
| microSD MOSI / CMD | GPIO35 | SPI/MMC block device | RP2350 PL022 SPI0 MOSI, SDIO command. |
| microSD MISO / DAT0 | GPIO36 | SPI/MMC block device | RP2350 PL022 SPI0 MISO, SDIO data0. |
| microSD CS / DAT3 | GPIO39 | SPI/MMC GPIO chip select | Driven through `cs-gpios`; native PL022 CS returned zero OCR during bring-up. |
| microSD card detect | GPIO33 | Pull-up input configured | Note: Adafruit Learn text has listed GPIO34 in one place, but the current CircuitPython board definition maps card detect to GPIO33. |
| SDIO DAT1 | GPIO37 | Not implemented | Later optional SDIO path. |
| SDIO DAT2 | GPIO38 | Not implemented | Later optional SDIO path. |
| HSTX DVI CKN/CKP | GPIO12/GPIO13 | `/dev/fruitjam-dvi` partial path | Tiny RGB332 HSTX frame helper; UART remains fallback while full console/fbdev work continues. |
| HSTX DVI D0N/D0P | GPIO14/GPIO15 | `/dev/fruitjam-dvi` partial path | Used by the bounded dashboard/text/test frame helper. |
| HSTX DVI D1N/D1P | GPIO16/GPIO17 | `/dev/fruitjam-dvi` partial path | Used by the bounded dashboard/text/test frame helper. |
| HSTX DVI D2N/D2P | GPIO18/GPIO19 | `/dev/fruitjam-dvi` partial path | Used by the bounded dashboard/text/test frame helper. |
| USB host D+ | GPIO1 | `/dev/fruitjam-usbhost` line/reset bridge plus PIO2 packet engine | Connected through the Fruit Jam PIO USB host path; descriptor/HID decode and experimental boot-keyboard `kbd-init`/`kbd-poll` commands are present. |
| USB host D- | GPIO2 | `/dev/fruitjam-usbhost` line/reset bridge plus PIO2 packet engine | Connected through the Fruit Jam PIO USB host path; descriptor/HID decode and experimental boot-keyboard `kbd-init`/`kbd-poll` commands are present. |
| USB host 5V power | GPIO11 | `/dev/fruitjam-usbhost` and `fruitjamctl` power switch | Power-enable/control for USB host 5 V path; general hub/composite USB host support is not implemented. |
| ESP32-C6 SPI SCK | GPIO30 | AirLift userspace SPI | RP2350 PL022/spidev path used by `airliftctl`. |
| ESP32-C6 SPI MOSI | GPIO31 | AirLift userspace SPI | Shared board SPI MOSI, used by `airliftctl`. |
| ESP32-C6 SPI MISO | GPIO28 | AirLift userspace SPI | Shared board SPI MISO, used by `airliftctl`. |
| ESP32-C6 SPI CS | GPIO46 | AirLift userspace SPI | Controlled by the Fruit Jam AirLift helper driver for `airliftctl`. |
| ESP32-C6 IRQ | GPIO23 | AirLift userspace SPI | Also shared as TLV320 DAC interrupt according to CircuitPython pin names. |
| ESP32-C6 busy | GPIO3 | AirLift userspace SPI | Used by the NINA SPI coprocessor protocol. |
| Peripheral reset | GPIO22 | `fruitjamctl` / AirLift reset line | Shared ESP32-C6/TLV320 reset. |
| I2C SDA | GPIO20 | `/dev/i2c-0` | GPIO-backed `i2c-gpio0`; `fruitjam-i2c scan` found TLV320DAC3100 at `0x18`. |
| I2C SCL | GPIO21 | `/dev/i2c-0` | GPIO-backed `i2c-gpio0`; internal pull-up enabled by the RP2350 SIO GPIO driver. |
| I2S DIN | GPIO24 | `/dev/fruitjam-audio` tone path | PIO1-generated 16-bit stereo sample stream for the first TLV320 tone milestone. |
| I2S MCLK | GPIO25 | `/dev/fruitjam-audio` tone path | PIO1-generated 15 MHz MCLK used as the TLV320 PLL input. |
| I2S BCLK | GPIO26 | `/dev/fruitjam-audio` tone path | PIO1-generated BCLK for the 8 kHz TLV320 I2S tone stream. |
| I2S WS | GPIO27 | `/dev/fruitjam-audio` tone path | PIO1-generated word-select clock for the 8 kHz TLV320 I2S tone stream. |
