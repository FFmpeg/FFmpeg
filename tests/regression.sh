#!/bin/sh
#
# automatic regression test for ffmpeg
#
#
#set -x
set -e

logfile="/tmp/ffmpeg.regression"

# tests to do
if [ "$1" = "mpeg4" ] ; then
    do_mpeg4=y
elif [ "$1" = "mpeg" ] ; then
    do_mpeg=y
elif [ "$1" = "libavtest" ] ; then
    do_libav=y
    logfile="/tmp/libav.regression"
else
    do_mpeg=y
    do_msmpeg4=y
    do_h263=y
    do_mpeg4=y
    do_mjpeg=y
    #do_rv10=y #broken
    do_mp2=y
    do_ac3=y
fi


# various files
ffmpeg="../ffmpeg"
outfile="/tmp/a-"
reffile="$2"
benchfile="/tmp/ffmpeg.bench"
raw_src="vsynth1/%d.pgm"
raw_dst="/tmp/out.yuv"
pcm_src="asynth1.sw"
pcm_dst="/tmp/out.wav"

function do_ffmpeg ()
{
    f="$1"
    shift
    echo $ffmpeg -bitexact $*
    $ffmpeg -bitexact -benchmark $* > /tmp/bench.tmp
    md5sum $f >> $logfile
    expr match "`cat /tmp/bench.tmp`" '.*utime=\(.*s\)' > /tmp/bench2.tmp
    echo `cat /tmp/bench2.tmp` $f >> $benchfile
}

function do_ffmpeg_crc ()
{
    f="$1"
    shift
    echo $ffmpeg -y -bitexact $* -f crc /tmp/ffmpeg.crc
    $ffmpeg -y -bitexact $* -f crc /tmp/ffmpeg.crc
    echo -n "$f " >> $logfile
    cat /tmp/ffmpeg.crc >> $logfile
}

echo "ffmpeg regression test" > $logfile
echo "ffmpeg benchmarks" > $benchfile

###################################
if [ -n "$do_mpeg" ] ; then
# mpeg1 encoding
file=${outfile}mpeg1.mpg
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -f mpeg1video $file 

# mpeg1 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst

# mpeg2 decoding
#do_ffmpeg /tmp/out-mpeg2.yuv -y -f mpegvideo -i a.vob \
#          -f rawvideo /tmp/out-mpeg2.yuv
fi

###################################
if [ -n "$do_msmpeg4" ] ; then
# msmpeg4 encoding
file=${outfile}msmpeg4.avi
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -an -vcodec msmpeg4 $file

# msmpeg4 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_h263" ] ; then
# h263 encoding
file=${outfile}h263.avi
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -s 352x288 -an -vcodec h263 $file

# h263p decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_mpeg4" ] ; then
# mpeg4
file=${outfile}odivx.avi
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -an -vcodec mpeg4 $file

# mpeg4 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_mjpeg" ] ; then
# mjpeg
file=${outfile}mjpeg.avi
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -an -vcodec mjpeg $file

# mjpeg decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_rv10" ] ; then
# rv10 encoding
file=${outfile}rv10.rm
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -an $file 

# rv10 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_mp2" ] ; then
# mp2 encoding
file=${outfile}mp2.mp2
do_ffmpeg $file -y -ab 128 -ac 2 -ar 44100 -f s16le -i $pcm_src $file 

# mp2 decoding
do_ffmpeg $pcm_dst -y -i $file -f wav $pcm_dst 
fi

###################################
if [ -n "$do_ac3" ] ; then
# ac3 encoding
file=${outfile}ac3.rm
do_ffmpeg $file -y -ab 128 -ac 2 -f s16le  -i $pcm_src -vn $file 

# ac3 decoding
do_ffmpeg $pcm_dst -y -i $file -f wav $pcm_dst 
fi

###################################
# libav testing
###################################

if [ -n "$do_libav" ] ; then

# avi
file=${outfile}libav.avi
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# asf
file=${outfile}libav.asf
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# rm
file=${outfile}libav.rm
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
# broken
#do_ffmpeg_crc $file -i $file

# mpegps
file=${outfile}libav.mpg
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# swf (decode audio only)
file=${outfile}libav.swf
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# ffm
file=${outfile}libav.ffm
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# XXX: need mov and mpegts tests (add bitstreams or add output capability in ffmpeg)

####################
# streamed images
# mjpeg
file=${outfile}libav.mjpeg
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src $file
do_ffmpeg_crc $file -i $file

# pgmpipe
file=${outfile}libav.pgmpipe
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f pgmpipe $file
do_ffmpeg_crc $file -f pgmpipe -i $file

# ppmpipe
file=${outfile}libav.ppmpipe
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f ppmpipe $file
do_ffmpeg_crc $file -f ppmpipe -i $file

# gif
file=${outfile}libav.gif
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f ppmpipe $file
#do_ffmpeg_crc $file -f ppmpipe -i $file

####################
# image formats
# pgm (we do not do md5 on image files yet)
file=${outfile}libav%d.pgm
$ffmpeg -t 0.5 -y -qscale 10 -f pgmyuv -i $raw_src $file
do_ffmpeg_crc $file -i $file

# ppm (we do not do md5 on image files yet)
file=${outfile}libav%d.ppm
$ffmpeg -t 0.5 -y -qscale 10 -f pgmyuv -i $raw_src $file
do_ffmpeg_crc $file -i $file

# jpeg (we do not do md5 on image files yet)
file=${outfile}libav%d.jpg
$ffmpeg -t 0.5 -y -qscale 10 -f pgmyuv -i $raw_src $file
do_ffmpeg_crc $file -i $file

####################
# audio only

# wav
file=${outfile}libav.wav
do_ffmpeg $file -t 1 -y -qscale 10 -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# alaw
file=${outfile}libav.al
do_ffmpeg $file -t 1 -y -qscale 10 -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# mulaw
file=${outfile}libav.ul
do_ffmpeg $file -t 1 -y -qscale 10 -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# au
file=${outfile}libav.au
do_ffmpeg $file -t 1 -y -qscale 10 -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

fi



if diff -u $logfile $reffile ; then
    echo 
    echo Regression test succeeded.
    exit 0
else
    echo 
    echo Regression test: Error.
    exit 1
fi
