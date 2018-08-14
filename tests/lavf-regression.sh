#!/bin/sh
#
# automatic regression test for libavformat
#
#
#set -x

set -e

. $(dirname $0)/regression-funcs.sh

eval do_$test=y

do_lavf()
{
    file=${outfile}lavf.$1
    do_avconv $file $DEC_OPTS -f image2 -c:v pgmyuv -i $raw_src $DEC_OPTS -ar 44100 -f s16le $2 -i $pcm_src $ENC_OPTS -b:a 64k -t 1 -qscale:v 10 $3
    test $5 = "disable_crc" ||
        do_avconv_crc $file $DEC_OPTS -i $target_path/$file $4
}

if [ -n "$do_avi" ] ; then
do_lavf avi "" "-c:a mp2 -ar 44100"
fi

if [ -n "$do_asf" ] ; then
do_lavf asf "" "-c:a mp2 -ar 44100" "-r 25"
fi

if [ -n "$do_rm" ] ; then
file=${outfile}lavf.rm
# The RealMedia muxer is broken.
do_lavf rm "" "-c:a ac3_fixed" "" disable_crc
fi

if [ -n "$do_mpg" ] ; then
do_lavf mpg "" "-ar 44100"
fi

if [ -n "$do_mxf" ] ; then
do_lavf mxf "-ar 48000" "-bf 2 -timecode_frame_start 264363"
fi

if [ -n "$do_mxf_d10" ]; then
do_lavf mxf_d10 "-ar 48000 -ac 2" "-r 25 -vf scale=720:576,pad=720:608:0:32 -c:v mpeg2video -g 0 -flags +ildct+low_delay -dc 10 -non_linear_quant 1 -intra_vlc 1 -qscale 1 -ps 1 -qmin 1 -rc_max_vbv_use 1 -rc_min_vbv_use 1 -pix_fmt yuv422p -minrate 30000k -maxrate 30000k -b 30000k -bufsize 1200000 -top 1 -rc_init_occupancy 1200000 -qmax 12 -f mxf_d10"
fi

if [ -n "$do_ts" ] ; then
do_lavf ts "" "-mpegts_transport_stream_id 42 -ar 44100"
fi

if [ -n "$do_swf" ] ; then
do_lavf swf "" "-an"
fi

if [ -n "$do_flv_fmt" ] ; then
do_lavf flv "" "-an"
fi

if [ -n "$do_mov" ] ; then
do_lavf mov "" "-c:a pcm_alaw -c:v mpeg4"
fi

if [ -n "$do_dv_fmt" ] ; then
do_lavf dv "-ar 48000 -channel_layout stereo" "-r 25 -s pal"
fi

if [ -n "$do_gxf" ] ; then
do_lavf gxf "-ar 48000" "-r 25 -s pal -ac 1"
fi

if [ -n "$do_nut" ] ; then
do_lavf nut "" "-c:a mp2 -ar 44100"
fi

if [ -n "$do_mkv" ] ; then
do_lavf mkv "" "-c:a mp2 -c:v mpeg4 -ar 44100"
fi


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
