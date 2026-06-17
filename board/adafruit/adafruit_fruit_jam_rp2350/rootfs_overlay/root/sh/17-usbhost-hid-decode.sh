#!/bin/sh
set -e

out=/tmp/fruitjam-usbhost-hid-test.out
rm -f "$out"

fruitjam-usbhost hid f04b0000040000000000abcd > "$out"
if ! grep -q 'usbhost hid boot-keyboard modifiers=0x00 keys=a(0x04)' "$out"; then
	cat "$out"
	echo "fruitjam-usbhost hid did not decode packet key a"
	rm -f "$out"
	exit 1
fi
if ! grep -q 'usbhost hid-text "a"' "$out"; then
	cat "$out"
	echo "fruitjam-usbhost hid did not decode packet text a"
	rm -f "$out"
	exit 1
fi

fruitjam-usbhost hid 0200050000000000 > "$out"
if ! grep -q 'usbhost hid boot-keyboard modifiers=0x02 keys=b(0x05)' "$out"; then
	cat "$out"
	echo "fruitjam-usbhost hid did not decode raw shifted key b"
	rm -f "$out"
	exit 1
fi
if ! grep -q 'usbhost hid-text "B"' "$out"; then
	cat "$out"
	echo "fruitjam-usbhost hid did not decode raw shifted text B"
	rm -f "$out"
	exit 1
fi

if fruitjam-usbhost hid f04b12010002000000403412 > "$out" 2>&1; then
	cat "$out"
	echo "fruitjam-usbhost hid accepted a device descriptor as keyboard input"
	rm -f "$out"
	exit 1
fi
if ! grep -q 'not-boot-keyboard-report' "$out"; then
	cat "$out"
	echo "fruitjam-usbhost hid did not explain descriptor rejection"
	rm -f "$out"
	exit 1
fi

rm -f "$out"
echo "17-usbhost-hid-decode.sh: ok"
