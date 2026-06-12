#!/bin/sh
set -e

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

echo "04-buttons-read.sh: ok"
