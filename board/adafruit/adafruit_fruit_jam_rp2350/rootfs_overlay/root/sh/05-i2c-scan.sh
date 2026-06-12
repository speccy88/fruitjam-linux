#!/bin/sh
set -e

echo "fruitjam-i2c: checking TLV320DAC3100 at 0x18 on /dev/i2c-0"
exec fruitjam-i2c ping 0x18
