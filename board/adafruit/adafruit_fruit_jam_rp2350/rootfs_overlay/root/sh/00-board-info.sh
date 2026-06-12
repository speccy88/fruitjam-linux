#!/bin/sh
set -e

check_exec() {
	if [ -x "$1" ]; then
		echo "$1 executable"
	else
		echo "$1 missing or not executable"
		exit 1
	fi
}

check_path() {
	if [ -e "$1" ]; then
		echo "$1 present"
	else
		echo "$1 missing"
		exit 1
	fi
}

echo "== kernel =="
read kernel < /proc/version
echo "$kernel"

echo "== uptime =="
read uptime < /proc/uptime
echo "$uptime"

echo "== mounts =="
while read dev path type rest
do
	if [ "$path" = "/" ] || [ "$path" = "/mnt/sd" ]; then
		echo "$dev $path $type"
	fi
done < /proc/mounts || true

echo "== key files =="
check_exec /usr/bin/berry
check_exec /usr/bin/berry-run
check_exec /usr/bin/fruitjam-rtttl
check_exec /usr/bin/fruitjam-wavplay
check_exec /usr/bin/fruitjam-adc
check_exec /usr/bin/fruitjam-dvi
check_exec /usr/bin/fruitjam-i2c
check_exec /usr/bin/fruitjam-usbhost
check_exec /usr/bin/wget
check_path /dev/neopixels
check_path /dev/fruitjam-audio
check_path /dev/fruitjam-dvi
check_path /dev/i2c-0
check_path /mnt/sd

echo "00-board-info.sh: ok"
