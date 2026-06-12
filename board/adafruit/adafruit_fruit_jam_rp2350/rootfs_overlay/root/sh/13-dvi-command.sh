#!/bin/sh
set -e

if [ "$#" -gt 0 ]; then
	fruitjam-dvi exec "$@"
else
	fruitjam-dvi exec fruitjam-services status
fi
