#!/bin/sh
set -e

found=0
while read dev path type rest
do
	if [ "$path" = "/mnt/sd" ]; then
		echo "$dev $path $type"
		found=1
	fi
done < /proc/mounts || true

if [ "$found" != 1 ]; then
	echo "/mnt/sd is not mounted"
	exit 1
fi

if [ -d /mnt/sd ]; then
	echo "/mnt/sd directory ok"
else
	echo "/mnt/sd directory missing"
	exit 1
fi

path=/mnt/sd/fruitjam-sh-test.txt
text="fruitjam sd write/read/remove ok"

echo "$text" > "$path"
read got < "$path"
if [ "$got" != "$text" ]; then
	echo "$path readback mismatch"
	exit 1
fi
rm "$path"
if [ -e "$path" ]; then
	echo "$path still exists after rm"
	exit 1
fi

echo "07-sd-card.sh: ok"
