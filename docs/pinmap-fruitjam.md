# Adafruit Fruit Jam RP2350B pin map

This document records board wiring relevant to the no-MMU Linux port. The current
image uses PSRAM, flash, UART, USB CDC, GPIO diagnostics, and the onboard NeoPixels;
the remaining entries are planning notes for later milestones.

| Function | Fruit Jam / RP2350B GPIO | Linux milestone status | Notes |
| --- | ---: | --- | --- |
| QSPI flash | Dedicated RP2350 QSPI pins | Milestone A used by boot ROM/XIP | 16 MiB flash, represented as `phram` at `0x10000000` in the DT. |
| PSRAM CS | GPIO47 | Milestone A bootloader | QMI/XIP CS1. Confirmed from Adafruit CircuitPython Fruit Jam board definition; recheck the Eagle schematic net if respinning. |
| PSRAM data/clock | Dedicated QMI/XIP pins | Milestone A bootloader | 8 MiB external PSRAM mapped at `0x11000000`. |
| UART console TX | GPIO8 | Milestone A | Fruit Jam labeled `TX`, RP2350 UART1 TX. |
| UART console RX | GPIO9 | Milestone A | Fruit Jam labeled `RX`, RP2350 UART1 RX. |
| Red LED / IR | GPIO29 | Bootloader + `fruitjamctl` | Active low LED; shared with IR receiver. |
| Button 1 / BOOT | GPIO0 | `fruitjamctl` input diagnostic | Active-low button. Holding at reset enters ROM boot. |
| Button 2 | GPIO4 | `fruitjamctl` input diagnostic | Active-low button. |
| Button 3 | GPIO5 | `fruitjamctl` input diagnostic | Active-low button. |
| NeoPixels | GPIO32 | `/dev/neopixels` | Five onboard WS2812-style status NeoPixels, driven by a PIO-backed misc driver. |
| microSD SCK | GPIO34 | Not implemented | SPI-mode first; also SDIO clock. |
| microSD MOSI / CMD | GPIO35 | Not implemented | SPI MOSI, SDIO command. |
| microSD MISO / DAT0 | GPIO36 | Not implemented | SPI MISO, SDIO data0. |
| microSD CS / DAT3 | GPIO39 | Not implemented | SPI chip select, SDIO data3. |
| microSD card detect | GPIO33 | Not implemented | Note: Adafruit Learn text has listed GPIO34 in one place, but the current CircuitPython board definition maps card detect to GPIO33. Treat this as a schematic-review item before enabling SD. |
| SDIO DAT1 | GPIO37 | Not implemented | Later optional SDIO path. |
| SDIO DAT2 | GPIO38 | Not implemented | Later optional SDIO path. |
| HSTX DVI CKN/CKP | GPIO12/GPIO13 | Not implemented | UART must remain fallback when DVI work starts. |
| HSTX DVI D0N/D0P | GPIO14/GPIO15 | Not implemented | Text console only in later milestone. |
| HSTX DVI D1N/D1P | GPIO16/GPIO17 | Not implemented | Text console only in later milestone. |
| HSTX DVI D2N/D2P | GPIO18/GPIO19 | Not implemented | Text console only in later milestone. |
| USB host D+ | GPIO1 | Not implemented | Connected through the Fruit Jam USB host/hub path. |
| USB host D- | GPIO2 | Not implemented | Connected through the Fruit Jam USB host/hub path. |
| USB host 5V power | GPIO11 | `fruitjamctl` power switch only | Power-enable/control for USB host 5 V path; Linux USB host protocol driver is not implemented. |
| ESP32-C6 SPI SCK | GPIO30 | Not implemented | AirLift WiFi is SPI coprocessor work for a later milestone. |
| ESP32-C6 SPI MOSI | GPIO31 | Not implemented | Shared board SPI MOSI. |
| ESP32-C6 SPI MISO | GPIO28 | Not implemented | Shared board SPI MISO. |
| ESP32-C6 SPI CS | GPIO46 | Not implemented | AirLift WiFi is SPI coprocessor work for a later milestone. |
| ESP32-C6 IRQ | GPIO23 | Not implemented | Also shared as TLV320 DAC interrupt according to CircuitPython pin names. |
| ESP32-C6 busy | GPIO3 | Not implemented | AirLift WiFi is SPI coprocessor work for a later milestone. |
| Peripheral reset | GPIO22 | `fruitjamctl` reset line | Shared ESP32-C6/TLV320 reset. |
| I2S DIN | GPIO24 | Not implemented | TLV320DAC3100 data input. |
| I2S MCLK | GPIO25 | Not implemented | TLV320DAC3100 master clock. |
| I2S BCLK | GPIO26 | Not implemented | TLV320DAC3100 bit clock. |
| I2S WS | GPIO27 | Not implemented | TLV320DAC3100 word select/LR clock. |
