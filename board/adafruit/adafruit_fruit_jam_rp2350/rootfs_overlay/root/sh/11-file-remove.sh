#!/bin/sh
set -e

path=/tmp/fruitjam-rm-test.txt

echo "remove me" > "$path"
if [ ! -e "$path" ]; then
	echo "$path was not created"
	exit 1
fi

rm "$path"
if [ -e "$path" ]; then
	echo "$path still exists after rm"
	exit 1
fi

echo "11-file-remove.sh: ok"
