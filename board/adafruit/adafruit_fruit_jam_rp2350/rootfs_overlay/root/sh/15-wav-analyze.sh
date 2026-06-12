#!/bin/sh
set -e

for wav in /mnt/sd/wavs/*.wav /mnt/sd/wavs/*.WAV
do
	if [ -f "$wav" ]; then
		echo "15-wav-analyze.sh: analyzing $wav"
		fruitjam-wavplay --analyze "$wav"
		echo "15-wav-analyze.sh: ok"
		exit 0
	fi
done

echo "15-wav-analyze.sh: skipped; no WAV files in /mnt/sd/wavs"
