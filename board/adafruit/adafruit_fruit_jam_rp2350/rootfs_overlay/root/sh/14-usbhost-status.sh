#!/bin/sh
set -e

fruitjam-usbhost status
fruitjam-usbhost reset 50

echo "14-usbhost-status.sh: ok"
