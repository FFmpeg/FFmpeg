#!/bin/sh
#
# automatic regression test for ffmpeg
#
#
#set -x
# Even in the 21st century some diffs are not supporting -u.
diff -u $0 $0 > /dev/null 2>&1
if [ $? -eq 0 ]; then
  diff_cmd="diff -u"
else
  diff_cmd="diff"
fi

set -e

datadir="./data"

logfile="$datadir/ffmpeg.regression"
outfile="$datadir/a-"

# tests to do
if [ "$1" = "mpeg4" ] ; then
    do_mpeg4=y
elif [ "$1" = "mpeg" ] ; then
    do_mpeg=y
elif [ "$1" = "ac3" ] ; then
    do_ac3=y
elif [ "$1" = "libavtest" ] ; then
    do_libav=y
    logfile="$datadir/libav.regression"
    outfile="$datadir/b-"
else
    do_mpeg=y
    do_mpeg2=y
    do_msmpeg4v2=y
    do_msmpeg4=y
    do_wmv1=y
    do_wmv2=y
    do_h263=y
    do_h263p=y
    do_mpeg4=y
    do_huffyuv=y
    do_mjpeg=y
    do_ljpeg=y
    do_rv10=y
    do_mp2=y
    do_ac3=y
    do_rc=y
    do_mpeg4adv=y
    do_mpeg1b=y
    do_asv1=y
    do_asv2=y
    do_flv=y
    do_ffv1=y
fi


# various files
ffmpeg="../ffmpeg_g"
tiny_psnr="./tiny_psnr"
reffile="$2"
benchfile="$datadir/ffmpeg.bench"
raw_src="$3/%d.pgm"
raw_dst="$datadir/out.yuv"
raw_ref="$datadir/ref.yuv"
pcm_src="asynth1.sw"
pcm_dst="$datadir/out.wav"

# create the data directory if it does not exists
mkdir -p $datadir

do_ffmpeg()
{
    f="$1"
    shift
    echo $ffmpeg -y -bitexact -dct_algo 1 -idct_algo 2 $*
    $ffmpeg -y -bitexact -dct_algo 1 -idct_algo 2 -benchmark $* > $datadir/bench.tmp 2> /tmp/ffmpeg$$
    egrep -v "^(Stream|Press|Input|Output|frame|  Stream|  Duration)" /tmp/ffmpeg$$ || true
    rm -f /tmp/ffmpeg$$
    md5sum -b $f >> $logfile
    if [ $f = $raw_dst ] ; then
        $tiny_psnr $f $raw_ref >> $logfile
    fi
    expr "`cat $datadir/bench.tmp`" : '.*utime=\(.*s\)' > $datadir/bench2.tmp
    echo `cat $datadir/bench2.tmp` $f >> $benchfile
}

do_ffmpeg_crc()
{
    f="$1"
    shift
    echo $ffmpeg -y -bitexact -dct_algo 1 -idct_algo 2 $* -f crc $datadir/ffmpeg.crc
    $ffmpeg -y -bitexact -dct_algo 1 -idct_algo 2 $* -f crc $datadir/ffmpeg.crc > /tmp/ffmpeg$$ 2>&1
    egrep -v "^(Stream|Press|Input|Output|frame|  Stream|  Duration)" /tmp/ffmpeg$$ || true
    rm -f /tmp/ffmpeg$$ 
    echo "$f `cat $datadir/ffmpeg.crc`" >> $logfile
}

do_ffmpeg_nocheck()
{
    f="$1"
    shift
    echo $ffmpeg -y -bitexact -dct_algo 1 -idct_algo 2 $*
    $ffmpeg -y -bitexact -dct_algo 1 -idct_algo 2 -benchmark $* > $datadir/bench.tmp 2> /tmp/ffmpeg$$
    egrep -v "^(Stream|Press|Input|Output|frame|  Stream|  Duration)" /tmp/ffmpeg$$ || true
    rm -f /tmp/ffmpeg$$
    expr "`cat $datadir/bench.tmp`" : '.*utime=\(.*s\)' > $datadir/bench2.tmp
    echo `cat $datadir/bench2.tmp` $f >> $benchfile
}

echo "ffmpeg regression test" > $logfile
echo "ffmpeg benchmarks" > $benchfile

###################################
# generate reference for quality check
do_ffmpeg_nocheck $raw_ref -y -f pgmyuv -i $raw_src -an -f rawvideo $raw_ref

###################################
if [ -n "$do_mpeg" ] ; then
# mpeg1 encoding
file=${outfile}mpeg1.mpg
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -f mpeg1video $file 

# mpeg1 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_mpeg2" ] ; then
# mpeg2 encoding
file=${outfile}mpeg2.vob
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -vcodec mpeg2video $file 

# mpeg2 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_msmpeg4v2" ] ; then
# msmpeg4 encoding
file=${outfile}msmpeg4v2.avi
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -an -vcodec msmpeg4v2 $file

# msmpeg4v2 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
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
if [ -n "$do_wmv1" ] ; then
# wmv1 encoding
file=${outfile}wmv1.avi
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -an -vcodec wmv1 $file

# wmv1 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_wmv2" ] ; then
# wmv2 encoding
file=${outfile}wmv2.avi
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -an -vcodec wmv2 $file

# wmv2 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_h263" ] ; then
# h263 encoding
file=${outfile}h263.avi
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -s 352x288 -an -vcodec h263 $file

# h263 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_h263p" ] ; then
# h263p encoding
file=${outfile}h263p.avi
do_ffmpeg $file -y -qscale 2 -umv -f pgmyuv -i $raw_src -s 352x288 -an -vcodec h263p -ps 300 $file

# h263p decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_mpeg4" ] ; then
# mpeg4
file=${outfile}odivx.mp4
do_ffmpeg $file -y -4mv -qscale 10 -f pgmyuv -i $raw_src -an -vcodec mpeg4 $file

# mpeg4 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_huffyuv" ] ; then
# huffyuv
file=${outfile}huffyuv.avi
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -an -vcodec huffyuv -strict -1 $file

# huffyuv decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo -strict -1 $raw_dst
fi

###################################
if [ -n "$do_rc" ] ; then
# mpeg4 rate control
file=${outfile}mpeg4-rc.avi
do_ffmpeg $file -y -b 400 -bf 2 -f pgmyuv -i $raw_src -an -vcodec mpeg4 $file

# mpeg4 rate control decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_mpeg4adv" ] ; then
# mpeg4
file=${outfile}mpeg4-adv.avi
do_ffmpeg $file -y -qscale 9 -4mv -hq -part -ps 200 -aic -f pgmyuv -i $raw_src -an -vcodec mpeg4 $file

# mpeg4 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_mpeg1b" ] ; then
# mpeg1
file=${outfile}mpeg1b.mpg
do_ffmpeg $file -y -qscale 8 -bf 3 -ps 200 -f pgmyuv -i $raw_src -an -vcodec mpeg1video $file

# mpeg1 decoding
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
if [ -n "$do_ljpeg" ] ; then
# ljpeg
file=${outfile}ljpeg.avi
do_ffmpeg $file -y -f pgmyuv -i $raw_src -an -vcodec ljpeg $file

# ljpeg decoding
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
if [ -n "$do_asv1" ] ; then
# asv1 encoding
file=${outfile}asv1.avi
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -an -vcodec asv1 $file

# asv1 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_asv2" ] ; then
# asv2 encoding
file=${outfile}asv2.avi
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -an -vcodec asv2 $file

# asv2 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_flv" ] ; then
# flv encoding
file=${outfile}flv.flv
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -an -vcodec flv $file

# flv decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst 
fi

###################################
if [ -n "$do_ffv1" ] ; then
# ffv1 encoding
file=${outfile}ffv1.avi
do_ffmpeg $file -y -strict -1 -f pgmyuv -i $raw_src -an -vcodec ffv1 $file

# ffv1 decoding
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
#do_ffmpeg $pcm_dst -y -i $file -f wav $pcm_dst 
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

# flv
file=${outfile}libav.flv
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# mov
#file=${outfile}libav.mov
#do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
#do_ffmpeg_crc $file -i $file

# nut
file=${outfile}libav.nut
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# XXX: need mpegts tests (add bitstreams or add output capability in ffmpeg)

####################
# streamed images
# mjpeg
#file=${outfile}libav.mjpeg
#do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src $file
#do_ffmpeg_crc $file -i $file

# pbmpipe
file=${outfile}libav.pbm
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f imagepipe $file
do_ffmpeg_crc $file -f imagepipe -i $file

# pgmpipe
file=${outfile}libav.pgm
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f imagepipe $file
do_ffmpeg_crc $file -f imagepipe -i $file

# ppmpipe
file=${outfile}libav.ppm
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f imagepipe $file
do_ffmpeg_crc $file -f imagepipe -i $file

# gif
file=${outfile}libav.gif
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src $file
#do_ffmpeg_crc $file -i $file

# yuv4mpeg
file=${outfile}libav.yuv4mpeg
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src $file
#do_ffmpeg_crc $file -i $file

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
#file=${outfile}libav%d.jpg
#$ffmpeg -t 0.5 -y -qscale 10 -f pgmyuv -i $raw_src $file
#do_ffmpeg_crc $file -i $file

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

####################
# pix_fmt conversions
conversions="yuv420p yuv422p yuv444p yuv422 yuv410p yuv411p yuvj420p \
             yuvj422p yuvj444p rgb24 bgr24 rgba32 rgb565 rgb555 gray monow \
	     monob pal8"
for pix_fmt in $conversions ; do
    file=${outfile}libav-${pix_fmt}.yuv
    do_ffmpeg_nocheck $file -r 1 -t 1 -y -f pgmyuv -i $raw_src \
                            -f rawvideo -s 352x288 -pix_fmt $pix_fmt $raw_dst
    do_ffmpeg $file -f rawvideo -s 352x288 -pix_fmt $pix_fmt -i $raw_dst \
                    -f rawvideo -s 352x288 -pix_fmt yuv444p $file
done

fi



if $diff_cmd $logfile $reffile ; then
    echo 
    echo Regression test succeeded.
    exit 0
else
    echo 
    echo Regression test: Error.
    exit 1
fi
