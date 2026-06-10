#!/bin/sh

port="${1:-7000}"
device="${2:-/dev/ttyGS0}"

echo "Serving ${device} on TCP port ${port} with nc"
echo "Connect with: nc <fruitjam-ip> ${port}"

while true; do
	nc -l -p "$port" < "$device" > "$device"
done
