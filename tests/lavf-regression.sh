#!/bin/sh
#
# automatic regression test for libavformat
#
#
#set -x

set -e

. $(dirname $0)/regression-funcs.sh

eval do_$test=y

# streamed images
# mjpeg
#file=${outfile}lavf.mjpeg
#do_avconv $file -t 1 -qscale 10 -f image2 -c:v pgmyuv -i $raw_src
#do_avconv_crc $file -i $target_path/$file

if [ -n "$do_gif" ] ; then
file=${outfile}lavf.gif
do_avconv $file $DEC_OPTS -f image2 -c:v pgmyuv -i $raw_src $ENC_OPTS -t 1 -qscale 10 -pix_fmt rgb24
do_avconv_crc $file $DEC_OPTS -i $target_path/$file -pix_fmt rgb24
fi

if [ -n "$do_yuv4mpeg" ] ; then
file=${outfile}lavf.y4m
do_avconv $file $DEC_OPTS -f image2 -c:v pgmyuv -i $raw_src $ENC_OPTS -t 1 -qscale 10
do_avconv_crc $file -i $target_path/$file
fi
