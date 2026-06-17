#!/bin/sh
set -e

section() {
	echo
	echo "== $* =="
}

np_one() {
	led="$1"
	{
		echo clear
		echo "set $led 0 24 24"
		echo write
	} > /dev/neopixels
}

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

echo "Fruit Jam example test runner"

section board-info
read kernel < /proc/version
echo "$kernel"
read uptime < /proc/uptime
echo "uptime $uptime"
while read dev path type rest
do
	if [ "$path" = "/" ] || [ "$path" = "/mnt/sd" ]; then
		echo "$dev $path $type"
	fi
done < /proc/mounts || true
check_exec /usr/bin/berry
check_exec /usr/bin/berry-run
check_exec /usr/bin/fruitjam-rtttl
check_exec /usr/bin/fruitjam-wavplay
check_exec /usr/bin/fruitjam-adc
check_exec /usr/bin/fruitjam-dvi
check_exec /usr/bin/fruitjam-i2c
check_exec /usr/bin/fruitjam-usbhost
check_exec /usr/bin/fruitjam-hidkeys
check_exec /usr/bin/fruitjam-mem
check_exec /usr/bin/free
check_exec /usr/bin/wget
check_path /dev/neopixels
check_path /dev/fruitjam-audio
check_path /dev/fruitjam-dvi
check_path /dev/i2c-0
check_path /mnt/sd

section services-http-cgi
fruitjam-services status
fruitjam-services httpd
wget -O - http://127.0.0.1/cgi-bin/env.cgi

section file-remove
rm_path=/tmp/fruitjam-rm-test.txt
echo "remove me" > "$rm_path"
if [ ! -e "$rm_path" ]; then
	echo "$rm_path was not created"
	exit 1
fi
rm "$rm_path"
if [ -e "$rm_path" ]; then
	echo "$rm_path still exists after rm"
	exit 1
fi
echo "file-remove: ok"

section neopixels-pattern
{
	echo clear
	echo "set 0 24 0 0"
	echo "set 1 24 12 0"
	echo "set 2 0 24 0"
	echo "set 3 0 0 24"
	echo "set 4 12 0 24"
	echo write
} > /dev/neopixels

section neopixels-chase
for led in 0 1 2 3 4
do
	np_one "$led"
done
{
	echo clear
	echo write
} > /dev/neopixels

section buttons
fruitjamctl buttons
fruitjam-buttons status
wait_gpio() {
	gpio="$1"
	if [ -e "/sys/class/gpio/gpio$gpio/value" ]; then
		return 0
	fi
	echo "$gpio" > /sys/class/gpio/export || true
	for n in 1 2 3 4 5 6 7 8 9 10
	do
		if [ -e "/sys/class/gpio/gpio$gpio/value" ]; then
			return 0
		fi
		usleep 2000
	done
	return 1
}
for gpio in 0 4 5
do
	wait_gpio "$gpio"
	read value < "/sys/class/gpio/gpio$gpio/value"
	echo "gpio$gpio value $value"
done

section memory
fruitjam-mem

section usbhost
sh /root/sh/14-usbhost-status.sh
sh /root/sh/16-hidkeys-decode.sh
sh /root/sh/17-usbhost-hid-decode.sh

section i2c
fruitjam-i2c ping 0x18

section adc
fruitjam-adc read 0 --samples 2
fruitjam-adc read 1 --samples 2
fruitjam-adc read temp --samples 1

section audio-clock
echo start > /dev/fruitjam-audio
echo stop > /dev/fruitjam-audio

section sd-card
sd_path=/mnt/sd/fruitjam-sh-test.txt
sd_text="fruitjam sd write/read/remove ok"
echo "$sd_text" > "$sd_path"
read sd_got < "$sd_path"
if [ "$sd_got" != "$sd_text" ]; then
	echo "$sd_path readback mismatch"
	exit 1
fi
rm "$sd_path"
if [ -e "$sd_path" ]; then
	echo "$sd_path still exists after rm"
	exit 1
fi
echo "sd-card: ok"

section wav-analyze
sh /root/sh/15-wav-analyze.sh

section dvi-dashboard
fruitjam-dvi dashboard

section dvi-command
fruitjam-dvi exec fruitjam-services status

section serial-over-tcp-help
echo "usage: serial-over-tcp.sh [PORT] [DEVICE]"
echo "default PORT=7000 DEVICE=/dev/ttyGS0"

section rtttl
echo "RTTTL examples are direct commands on this no-MMU image:"
echo "  fruitjam-rtttl scale"
echo "  fruitjam-rtttl --loud --tone 880 2500"

echo
echo "Fruit Jam shell examples: all tests passed"
