#!/bin/sh
# Write the data/ folder of max98357a-MP3 to the board's LittleFS partition.
#
# The sketch must have been flashed with the partition scheme
# "No OTA (1MB APP/3MB SPIFFS)" (noota_3g), whose filesystem partition starts
# at 0x110000 and is 0x2E0000 bytes long, on both the ESP32 and the ESP32-C3.
# This builds a LittleFS image of data/ with the core's mklittlefs, using the
# block and page sizes the Arduino LittleFS library expects, and writes it
# with the core's esptool, which detects the chip by itself.  It replaces
# whatever the filesystem held.
#
# Usage: ./upload-data.sh [port]   (default /dev/ttyUSB0; the C3 is usually
#                                   /dev/ttyACM0)
set -eu

port=${1:-/dev/ttyUSB0}
here=$(cd "$(dirname "$0")" && pwd)
tools=$HOME/.arduino15/packages/esp32/tools
mklittlefs=$(ls -d "$tools"/mklittlefs/*/mklittlefs | tail -1)
esptool=$(ls -d "$tools"/esptool_py/*/esptool | tail -1)

offset=0x110000
size=0x2E0000

image=$(mktemp)
trap 'rm -f "$image"' EXIT

"$mklittlefs" -c "$here/data" -b 4096 -p 256 -s $size "$image"
"$esptool" --port "$port" --baud 921600 write-flash $offset "$image"
