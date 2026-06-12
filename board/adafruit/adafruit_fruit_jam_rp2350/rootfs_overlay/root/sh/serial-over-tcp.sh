#!/bin/sh

usage() {
	echo "usage: serial-over-tcp.sh [PORT] [DEVICE]"
	echo "default PORT=7000 DEVICE=/dev/ttyGS0"
	echo "bridges DEVICE to a TCP listener with nc until interrupted"
}

case "$1" in
	-h|--help)
		usage
		exit 0
		;;
esac

port="${1:-7000}"
device="${2:-/dev/ttyGS0}"

echo "Serving ${device} on TCP port ${port} with nc"
echo "Connect with: nc <fruitjam-ip> ${port}"

while true
do
	nc -l -p "$port" < "$device" > "$device"
done
