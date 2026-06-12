#!/bin/sh
set -e

dev=/dev/neopixels

{
	echo clear
	echo "set 0 24 0 0"
	echo "set 1 24 12 0"
	echo "set 2 0 24 0"
	echo "set 3 0 0 24"
	echo "set 4 12 0 24"
	echo write
} > "$dev"

echo "02-neopixels-pattern.sh: ok"
