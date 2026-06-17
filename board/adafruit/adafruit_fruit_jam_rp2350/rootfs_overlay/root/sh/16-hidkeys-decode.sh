#!/bin/sh
set -e

out=/tmp/fruitjam-hidkeys-test.out
rm -f "$out"

fruitjam-hidkeys \
	0000040000000000 \
	0000000000000000 \
	0200050000000000 \
	0000000000000000 \
	f04b0000040000000000abcd \
	f04b0000000000000000abcd > "$out"
echo >> "$out"
read got < "$out"
rm -f "$out"

if [ "$got" != "aBa" ]; then
	echo "fruitjam-hidkeys decoded '$got', expected 'aBa'"
	exit 1
fi

fruitjam-hidkeys --events \
	f04b0000040000000000abcd \
	f04b0000000000000000abcd

echo "16-hidkeys-decode.sh: ok"
