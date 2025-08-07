#! /data/data/com.termux/files/usr/bin/bash

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd $DIR

pwd

INPUT=$(./find_input.sh)
OUTPUT="rimappato"
CURVE_STICK=4.0
CURVE_TRIG=2.0
DIAG_COMPENSATION=0.2

sudo chmod a+r $INPUT

killall analog_mapper

clang -Wcomment --target=aarch64-linux-android analog_mapper.c -O2 -o analog_mapper -lm && ./analog_mapper $INPUT $OUTPUT $CURVE_STICK $DIAG_COMPENSATION $CURVE_TRIG

#echo ./analog_mapper $INPUT $OUTPUT $CURVE_STICK $DIAG_COMPENSATION $CURVE_TRIG
#./analog_mapper $INPUT $OUTPUT $CURVE_STICK $DIAG_COMPENSATION $CURVE_TRIG

