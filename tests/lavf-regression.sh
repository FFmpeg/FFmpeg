#!/bin/sh
#
# automatic regression test for libavformat
#
#
#set -x

set -e

. $(dirname $0)/regression-funcs.sh

eval do_$test=y

ENC_OPTS="$ENC_OPTS -metadata title=lavftest"

do_lavf_fate()
{
    file=${outfile}lavf.$1
    input="${target_samples}/$2"
    do_avconv $file $DEC_OPTS -i "$input" $ENC_OPTS -vcodec copy -acodec copy
    do_avconv_crc $file $DEC_OPTS -i $target_path/$file $3
}

if [ -n "$do_mp3" ] ; then
do_lavf_fate mp3 "mp3-conformance/he_32khz.bit" "-acodec copy"
fi

if [ -n "$do_latm" ] ; then
do_lavf_fate latm "aac/al04_44.mp4" "-acodec copy"
fi

if [ -n "$do_ogg_vp3" ] ; then
# -idct simple causes different results on different systems
DEC_OPTS="$DEC_OPTS -idct auto"
do_lavf_fate ogg "vp3/coeff_level64.mkv"
fi

if [ -n "$do_ogg_vp8" ] ; then
do_lavf_fate ogv "vp8/RRSF49-short.webm" "-acodec copy"
fi

if [ -n "$do_mov_qtrle_mace6" ] ; then
DEC_OPTS="$DEC_OPTS -idct auto"
do_lavf_fate mov "qtrle/Animation-16Greys.mov"
fi

if [ -n "$do_avi_cram" ] ; then
DEC_OPTS="$DEC_OPTS -idct auto"
do_lavf_fate avi "cram/toon.avi"
fi


# streamed images
# mjpeg
#file=${outfile}lavf.mjpeg
#do_avconv $file -t 1 -qscale 10 -f image2 -vcodec pgmyuv -i $raw_src
#do_avconv_crc $file -i $target_path/$file

if [ -n "$do_gif" ] ; then
file=${outfile}lavf.gif
do_avconv $file $DEC_OPTS -f image2 -vcodec pgmyuv -i $raw_src $ENC_OPTS -t 1 -qscale 10 -pix_fmt rgb24
do_avconv_crc $file $DEC_OPTS -i $target_path/$file -pix_fmt rgb24
fi

if [ -n "$do_apng" ] ; then
file=${outfile}lavf.apng
do_avconv $file $DEC_OPTS -f image2 -vcodec pgmyuv -i $raw_src $ENC_OPTS -t 1 -pix_fmt rgb24
do_avconv_crc $file $DEC_OPTS -i $target_path/$file -pix_fmt rgb24
file_copy=${outfile}lavf.copy.apng
do_avconv $file_copy $DEC_OPTS -i $file $ENC_OPTS -c copy
do_avconv_crc $file_copy $DEC_OPTS -i $target_path/$file_copy
file=${outfile}lavf.png
do_avconv $file $DEC_OPTS -f image2 -vcodec pgmyuv -i $raw_src $ENC_OPTS -pix_fmt rgb24 -frames:v 1 -f apng
do_avconv_crc $file $DEC_OPTS -i $target_path/$file -pix_fmt rgb24
fi

if [ -n "$do_yuv4mpeg" ] ; then
file=${outfile}lavf.y4m
do_avconv $file $DEC_OPTS -f image2 -vcodec pgmyuv -i $raw_src $ENC_OPTS -t 1 -qscale 10
do_avconv_crc $file -i $target_path/$file
fi

if [ -n "$do_fits" ] ; then
pix_fmts="gray gray16be gbrp gbrap gbrp16be gbrap16be"
for pix_fmt in $pix_fmts ; do
    file=${outfile}${pix_fmt}lavf.fits
    do_avconv $file $DEC_OPTS -f image2 -vcodec pgmyuv -i $raw_src $ENC_OPTS -pix_fmt $pix_fmt
    do_avconv_crc $file $DEC_OPTS -i $target_path/$file -pix_fmt $pix_fmt
done
fi
