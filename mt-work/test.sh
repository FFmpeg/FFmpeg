#!/bin/bash

fn=`basename "$1"`
for th in 1 2 3 4; do
    time ./ffmpeg_g -threads $th -flags2 +fast  -vsync 0 -y -t 30 -i "$1" -an -f framecrc "crc/$fn-$th.txt" >/dev/null 2>&1
done

./ffmpeg_g -threads 1 -y -t 10 -i "$1" -an -f framecrc "crc/$fn-1-vsync.txt" >/dev/null 2>&1
./ffmpeg_g -threads 3 -y -t 10 -i "$1" -an -f framecrc "crc/$fn-3-vsync.txt" >/dev/null 2>&1

md5 "crc/$fn-"[1234].txt
echo
md5 "crc/$fn-"*vsync.txt
