#!/bin/sh
set -e

out=/tmp/fruitjam-hidkeys-test.out
rm -f "$out"

fruitjam-hidkeys \
	0000040000000000 \
	0000000000000000 \
	0200050000000000 \
	0000000000000000 > "$out"
echo >> "$out"
read got < "$out"
rm -f "$out"

if [ "$got" != "aB" ]; then
	echo "fruitjam-hidkeys decoded '$got', expected 'aB'"
	exit 1
fi

fruitjam-hidkeys --events \
	0000040000000000 \
	0000000000000000

echo "16-hidkeys-decode.sh: ok"
