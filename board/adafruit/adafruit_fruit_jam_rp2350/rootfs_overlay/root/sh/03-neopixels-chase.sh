#!/bin/sh
set -e

dev=/dev/neopixels

show_one() {
	led="$1"
	{
		echo clear
		echo "set $led 0 24 24"
		echo write
	} > "$dev"
}

for led in 0 1 2 3 4
do
	show_one "$led"
done

{
	echo clear
	echo write
} > "$dev"

echo "03-neopixels-chase.sh: ok"
