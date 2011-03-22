#!/bin/bash

valgrind --leak-check=full ./ffmpeg_g -threads 3 -vsync 0 -y -t 30 -i "$1" -an -f framecrc /dev/null