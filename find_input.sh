#! /data/data/com.termux/files/usr/bin/sh
C=$(grep '^N: Name\|^H:' /proc/bus/input/devices |   grep -A1 "Retroid Pocket Controller\|Xbox Wireless Controller" | tail -n  1 | cut -d " " -f 3)
C="/dev/input/$C"
echo $C
