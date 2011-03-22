#!/bin/bash

fn=`basename "$1"`
for th in 1 4; do
    time ./ffmpeg_g -threads $th -skip_loop_filter all -vsync 0 -y -t 30 -i "$1" -an -f rawvideo "raw/n-$fn-$th.yuv"
done

#for th in 1 4; do
#    time ./ffmpeg_g -threads $th -vsync 0 -y -t 30 -i "$1" -an -f rawvideo "raw/$fn-$th.yuv"
#done
