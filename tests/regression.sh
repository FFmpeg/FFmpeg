#!/bin/sh
#
# automatic regression test for ffmpeg
#
#
#set -x
# Even in the 21st century some diffs are not supporting -u.
diff -u "$0" "$0" > /dev/null 2>&1
if [ $? -eq 0 ]; then
  diff_cmd="diff -u"
else
  diff_cmd="diff"
fi

diff -w "$0" "$0" > /dev/null 2>&1
if [ $? -eq 0 ]; then
  diff_cmd="$diff_cmd -w"
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
    do_mpeg2=y
elif [ "$1" = "ac3" ] ; then
    do_ac3=y
elif [ "$1" = "huffyuv" ] ; then
    do_huffyuv=y
elif [ "$1" = "mpeg2thread" ] ; then
    do_mpeg2thread=y
elif [ "$1" = "snow" ] ; then
    do_snow=y
elif [ "$1" = "snowll" ] ; then
    do_snowll=y
elif [ "$1" = "libavtest" ] ; then
    do_libav=y
    logfile="$datadir/libav.regression"
    outfile="$datadir/b-"
else
    do_mpeg=y
    do_mpeg2=y
    do_mpeg2thread=y
    do_msmpeg4v2=y
    do_msmpeg4=y
    do_wmv1=y
    do_wmv2=y
    do_h261=y
    do_h263=y
    do_h263p=y
    do_mpeg4=y
    do_mp4psp=y
    do_huffyuv=y
    do_mjpeg=y
    do_ljpeg=y
    do_jpegls=y
    do_rv10=y
    do_rv20=y
    do_mp2=y
    do_ac3=y
    do_g726=y
    do_adpcm_ima_wav=y
    do_adpcm_ms=y
    do_flac=y
    do_rc=y
    do_mpeg4adv=y
    do_mpeg4thread=y
    do_mpeg4nr=y
    do_mpeg1b=y
    do_asv1=y
    do_asv2=y
    do_flv=y
    do_ffv1=y
    do_error=y
    do_svq1=y
    do_snow=y
    do_snowll=y
    do_adpcm_yam=y
    do_dv=y
    do_dv50=y
fi


# various files
ffmpeg="../ffmpeg_g"
tiny_psnr="./tiny_psnr"
reffile="$2"
benchfile="$datadir/ffmpeg.bench"
raw_src="$3/%02d.pgm"
raw_dst="$datadir/out.yuv"
raw_ref="$datadir/ref.yuv"
pcm_src="asynth1.sw"
pcm_dst="$datadir/out.wav"
pcm_ref="$datadir/ref.wav"
if [ X"`echo | md5sum 2> /dev/null`" != X ]; then
    do_md5sum() { md5sum -b $1; }
elif [ -x /sbin/md5 ]; then
    do_md5sum() { /sbin/md5 -r $1 | sed 's# \**\./# *./#'; }
else
    do_md5sum() { echo No md5sum program found; }
fi

# create the data directory if it does not exists
mkdir -p $datadir

FFMPEG_OPTS="-y -flags +bitexact -dct fastint -idct simple"

do_ffmpeg()
{
    f="$1"
    shift
    echo $ffmpeg $FFMPEG_OPTS $*
    $ffmpeg $FFMPEG_OPTS -benchmark $* > $datadir/bench.tmp 2> /tmp/ffmpeg$$
    egrep -v "^(Stream|Press|Input|Output|frame|  Stream|  Duration|video:)" /tmp/ffmpeg$$ || true
    rm -f /tmp/ffmpeg$$
    do_md5sum $f >> $logfile
    if [ $f = $raw_dst ] ; then
        $tiny_psnr $f $raw_ref >> $logfile
    elif [ $f = $pcm_dst ] ; then
        $tiny_psnr $f $pcm_ref 2 >> $logfile
    else
        wc -c $f >> $logfile
    fi
    expr "`cat $datadir/bench.tmp`" : '.*utime=\(.*s\)' > $datadir/bench2.tmp
    echo `cat $datadir/bench2.tmp` $f >> $benchfile
}

do_ffmpeg_crc()
{
    f="$1"
    shift
    echo $ffmpeg $FFMPEG_OPTS $* -f crc $datadir/ffmpeg.crc
    $ffmpeg $FFMPEG_OPTS $* -f crc $datadir/ffmpeg.crc > /tmp/ffmpeg$$ 2>&1
    egrep -v "^(Stream|Press|Input|Output|frame|  Stream|  Duration|video:|ffmpeg version|  configuration|  built)" /tmp/ffmpeg$$ || true
    rm -f /tmp/ffmpeg$$
    echo "$f `cat $datadir/ffmpeg.crc`" >> $logfile
}

do_ffmpeg_nocheck()
{
    f="$1"
    shift
    echo $ffmpeg $FFMPEG_OPTS $*
    $ffmpeg $FFMPEG_OPTS -benchmark $* > $datadir/bench.tmp 2> /tmp/ffmpeg$$
    egrep -v "^(Stream|Press|Input|Output|frame|  Stream|  Duration|video:)" /tmp/ffmpeg$$ || true
    rm -f /tmp/ffmpeg$$
    expr "`cat $datadir/bench.tmp`" : '.*utime=\(.*s\)' > $datadir/bench2.tmp
    echo `cat $datadir/bench2.tmp` $f >> $benchfile
}

do_video_decoding()
{
    do_ffmpeg $raw_dst -y -i $file -f rawvideo $@ $raw_dst
}

do_video_encoding()
{
    file=${outfile}$1
    do_ffmpeg $file -y $2 -f pgmyuv -i $raw_src $3 $file
}

do_audio_encoding()
{
    file=${outfile}$1
    do_ffmpeg $file -y -ab 128 -ac 2 -f s16le -i $pcm_src $3 $file
}

do_audio_decoding()
{
    do_ffmpeg $pcm_dst -y -i $file -f wav $pcm_dst
}

do_libav()
{
    file=${outfile}libav.$1
    do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $2 $file
    do_ffmpeg_crc $file -i $file $3

}

do_streamed_images()
{
    file=${outfile}libav.$1
    do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f image2pipe $file
    do_ffmpeg_crc $file -f image2pipe -i $file
}

do_image_formats()
{
    file=${outfile}libav%02d.$1
    $ffmpeg -t 0.5 -y -qscale 10 -f pgmyuv -i $raw_src $2 $3 $file
    do_ffmpeg_crc $file $3 -i $file

}

do_audio_only()
{
    file=${outfile}libav.$1
    do_ffmpeg $file -t 1 -y -qscale 10 -f s16le -i $pcm_src $file
    do_ffmpeg_crc $file -i $file
}

echo "ffmpeg regression test" > $logfile
echo "ffmpeg benchmarks" > $benchfile

###################################
# generate reference for quality check
do_ffmpeg_nocheck $raw_ref -y -f pgmyuv -i $raw_src -an -f rawvideo $raw_ref
do_ffmpeg_nocheck $pcm_ref -y -ab 128 -ac 2 -ar 44100 -f s16le -i $pcm_src -f wav $pcm_ref

###################################
if [ -n "$do_mpeg" ] ; then
# mpeg1 encoding
do_video_encoding mpeg1.mpg "-qscale 10" "-f mpeg1video"

# mpeg1 decoding
do_video_decoding
fi

###################################
if [ -n "$do_mpeg2" ] ; then
# mpeg2 encoding
do_video_encoding mpeg2.mpg "-qscale 10" "-vcodec mpeg2video -f mpeg1video"

# mpeg2 decoding
do_video_decoding

# mpeg2 encoding using intra vlc
do_video_encoding mpeg2ivlc.mpg "-qscale 10" "-vcodec mpeg2video -f mpeg1video -flags2 +ivlc"

# mpeg2 decoding
do_video_decoding

# mpeg2 encoding
do_video_encoding mpeg2.mpg "-qscale 10" "-vcodec mpeg2video -idct int -dct int -f mpeg1video"

# mpeg2 decoding
do_ffmpeg $raw_dst -y -idct int -i $file -f rawvideo $raw_dst

# mpeg2 encoding interlaced
do_video_encoding mpeg2i.mpg "-qscale 10" "-vcodec mpeg2video -f mpeg1video -flags +ildct+ilme"

# mpeg2 decoding
do_video_decoding
fi

###################################
if [ -n "$do_mpeg2thread" ] ; then
# mpeg2 encoding interlaced
do_video_encoding mpeg2thread.mpg "-qscale 10" "-vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -threads 2"

# mpeg2 decoding
do_video_decoding

# mpeg2 encoding interlaced using intra vlc
do_video_encoding mpeg2threadivlc.mpg "-qscale 10" "-vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -flags2 +ivlc -threads 2"

# mpeg2 decoding
do_video_decoding

# mpeg2 encoding interlaced
file=${outfile}mpeg2reuse.mpg
do_ffmpeg $file -y -sameq -me_threshold 256 -mb_threshold 1024 -i ${outfile}mpeg2thread.mpg -vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -threads 4 $file

# mpeg2 decoding
do_video_decoding
fi

###################################
if [ -n "$do_msmpeg4v2" ] ; then
# msmpeg4 encoding
do_video_encoding msmpeg4v2.avi "-qscale 10" "-an -vcodec msmpeg4v2"

# msmpeg4v2 decoding
do_video_decoding
fi

###################################
if [ -n "$do_msmpeg4" ] ; then
# msmpeg4 encoding
do_video_encoding msmpeg4.avi "-qscale 10" "-an -vcodec msmpeg4"

# msmpeg4 decoding
do_video_decoding
fi

###################################
if [ -n "$do_wmv1" ] ; then
# wmv1 encoding
do_video_encoding wmv1.avi "-qscale 10" "-an -vcodec wmv1"

# wmv1 decoding
do_video_decoding
fi

###################################
if [ -n "$do_wmv2" ] ; then
# wmv2 encoding
do_video_encoding wmv2.avi "-qscale 10" "-an -vcodec wmv2"

# wmv2 decoding
do_video_decoding
fi

###################################
if [ -n "$do_h261" ] ; then
# h261 encoding
do_video_encoding h261.avi "-qscale 11" "-s 352x288 -an -vcodec h261"

# h261 decoding
do_video_decoding
fi

###################################
if [ -n "$do_h263" ] ; then
# h263 encoding
do_video_encoding h263.avi "-qscale 10" "-s 352x288 -an -vcodec h263"

# h263 decoding
do_video_decoding
fi

###################################
if [ -n "$do_h263p" ] ; then
# h263p encoding
do_video_encoding h263p.avi "-qscale 2 -flags +umv+aiv+aic" "-s 352x288 -an -vcodec h263p -ps 300"

# h263p decoding
do_video_decoding
fi

###################################
if [ -n "$do_mpeg4" ] ; then
# mpeg4
do_video_encoding odivx.mp4 "-flags +mv4 -mbd bits -qscale 10" "-an -vcodec mpeg4"

# mpeg4 decoding
do_video_decoding
fi

###################################
if [ -n "$do_huffyuv" ] ; then
# huffyuv
do_video_encoding huffyuv.avi "" "-an -vcodec huffyuv -pix_fmt yuv422p"

# huffyuv decoding
do_video_decoding -strict -2 -pix_fmt yuv420p
fi

###################################
if [ -n "$do_rc" ] ; then
# mpeg4 rate control
do_video_encoding mpeg4-rc.avi "-b 400k -bf 2" "-an -vcodec mpeg4"

# mpeg4 rate control decoding
do_video_decoding
fi

###################################
if [ -n "$do_mpeg4adv" ] ; then
# mpeg4
do_video_encoding mpeg4-adv.avi "-qscale 9 -flags +mv4+part+aic+trell -mbd bits -ps 200" "-an -vcodec mpeg4"

# mpeg4 decoding
do_video_decoding
fi

###################################
if [ -n "$do_mpeg4thread" ] ; then
# mpeg4
do_video_encoding mpeg4-thread.avi "-b 500k -flags +mv4+part+aic+trell -mbd bits -ps 200 -bf 2" "-an -vcodec mpeg4 -threads 2"

# mpeg4 decoding
do_video_decoding
fi

###################################
if [ -n "$do_mpeg4adv" ] ; then
# mpeg4
do_video_encoding mpeg4-Q.avi "-qscale 7 -flags +mv4+qpel -mbd 2 -bf 2 -cmp 1 -subcmp 2" "-an -vcodec mpeg4"

# mpeg4 decoding
do_video_decoding
fi

###################################
if [ -n "$do_mp4psp" ] ; then
# mp4 PSP style
file=${outfile}mpeg4-PSP.mp4
do_ffmpeg $file -y -b 768k -s 320x240 -f psp -ar 24000 -ab 32 -i $raw_src $file
fi

###################################
if [ -n "$do_error" ] ; then
# damaged mpeg4
do_video_encoding error-mpeg4-adv.avi "-qscale 7 -flags +mv4+part+aic -mbd rd -ps 250 -error 10" "-an -vcodec mpeg4"

# damaged mpeg4 decoding
do_video_decoding
fi

###################################
if [ -n "$do_mpeg4nr" ] ; then
# noise reduction
do_video_encoding mpeg4-nr.avi "-qscale 8 -flags +mv4 -mbd rd -nr 200" "-an -vcodec mpeg4"

# mpeg4 decoding
do_video_decoding
fi

###################################
if [ -n "$do_mpeg1b" ] ; then
# mpeg1
do_video_encoding mpeg1b.mpg "-qscale 8 -bf 3 -ps 200" "-an -vcodec mpeg1video -f mpeg1video"

# mpeg1 decoding
do_video_decoding
fi

###################################
if [ -n "$do_mjpeg" ] ; then
# mjpeg
do_video_encoding mjpeg.avi "-qscale 10" "-an -vcodec mjpeg -pix_fmt yuvj420p"

# mjpeg decoding
do_video_decoding -pix_fmt yuv420p
fi

###################################
if [ -n "$do_ljpeg" ] ; then
# ljpeg
do_video_encoding ljpeg.avi "" "-an -vcodec ljpeg -strict -1"

# ljpeg decoding
do_video_decoding
fi

###################################
if [ -n "$do_jpegls" ] ; then
# jpeg ls
do_video_encoding jpegls.avi "" "-an -vcodec jpegls -vtag MJPG"

# jpeg ls decoding
do_video_decoding -pix_fmt yuv420p
fi

###################################
if [ -n "$do_rv10" ] ; then
# rv10 encoding
do_video_encoding rv10.rm "-qscale 10" "-an"

# rv10 decoding
do_video_decoding
fi

###################################
if [ -n "$do_rv20" ] ; then
# rv20 encoding
do_video_encoding rv20.rm "-qscale 10" "-vcodec rv20 -an"

# rv20 decoding
do_video_decoding
fi

###################################
if [ -n "$do_asv1" ] ; then
# asv1 encoding
do_video_encoding asv1.avi "-qscale 10" "-an -vcodec asv1"

# asv1 decoding
do_video_decoding
fi

###################################
if [ -n "$do_asv2" ] ; then
# asv2 encoding
do_video_encoding asv2.avi "-qscale 10" "-an -vcodec asv2"

# asv2 decoding
do_video_decoding
fi

###################################
if [ -n "$do_flv" ] ; then
# flv encoding
do_video_encoding flv.flv "-qscale 10" "-an -vcodec flv"

# flv decoding
do_video_decoding
fi

###################################
if [ -n "$do_ffv1" ] ; then
# ffv1 encoding
do_video_encoding ffv1.avi "-strict -2" "-an -vcodec ffv1"

# ffv1 decoding
do_video_decoding
fi

###################################
if [ -n "$do_snow" ] ; then
# snow encoding
do_video_encoding snow.avi "-strict -2" "-an -vcodec snow -qscale 2 -flags +qpel -me iter -dia_size 2 -cmp 12 -subcmp 12 -s 128x64"

# snow decoding
do_video_decoding -s 352x288
fi

###################################
if [ -n "$do_snowll" ] ; then
# snow encoding
do_video_encoding snow53.avi "-strict -2" "-an -vcodec snow -qscale .001 -pred 1 -flags +mv4+qpel"

# snow decoding
do_video_decoding
fi

###################################
if [ -n "$do_dv" ] ; then
# dv encoding
do_video_encoding dv.dv "-dct int" "-s pal -an"

# dv decoding
do_video_decoding -s cif
fi

###################################
if [ -n "$do_dv50" ] ; then
# dv50 encoding
do_video_encoding dv.dv "-dct int" "-s pal -pix_fmt yuv422p -an"

# dv50 decoding
do_video_decoding -s cif -pix_fmt yuv420p
fi


###################################
if [ -n "$do_svq1" ] ; then
# svq1 encoding
do_video_encoding svq1.mov "" "-an -vcodec svq1 -qscale 3 -pix_fmt yuv410p"

# svq1 decoding
do_video_decoding -pix_fmt yuv420p
fi

###################################
if [ -n "$do_mp2" ] ; then
# mp2 encoding
do_audio_encoding mp2.mp2 "-ar 44100"

# mp2 decoding
do_audio_decoding
$tiny_psnr $pcm_dst $pcm_ref 2 1924 >> $logfile
fi

###################################
if [ -n "$do_ac3" ] ; then
# ac3 encoding
do_audio_encoding ac3.rm "" -vn

# ac3 decoding
#do_audio_decoding
fi

###################################
if [ -n "$do_g726" ] ; then
# g726 encoding
do_audio_encoding g726.wav "-ar 44100" "-ab 32 -ac 1 -ar 8000 -acodec g726"

# g726 decoding
do_audio_decoding
fi

###################################
if [ -n "$do_adpcm_ima_wav" ] ; then
# encoding
do_audio_encoding adpcm_ima.wav "-ar 44100" "-acodec adpcm_ima_wav"

# decoding
do_audio_decoding
fi

###################################
if [ -n "$do_adpcm_ms" ] ; then
# encoding
do_audio_encoding adpcm_ms.wav "-ar 44100" "-acodec adpcm_ms"

# decoding
do_audio_decoding
fi

###################################
if [ -n "$do_adpcm_yam" ] ; then
# encoding
do_audio_encoding adpcm_yam.wav "-ar 44100" "-acodec adpcm_yamaha"

# decoding
do_audio_decoding
fi

###################################
if [ -n "$do_flac" ] ; then
# encoding
do_audio_encoding flac.flac "-ar 44100" "-acodec flac -compression_level 2"

# decoding
do_audio_decoding
fi

###################################
# libav testing
###################################

if [ -n "$do_libav" ] ; then

# avi
do_libav avi

# asf
do_libav asf "-acodec mp2" "-r 25"

# rm
file=${outfile}libav.rm
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
# broken
#do_ffmpeg_crc $file -i $file

# mpegps
do_libav mpg

# mpegts
do_libav ts

# swf (decode audio only)
do_libav swf "-acodec mp2"

# ffm
do_libav ffm

# flv
do_libav flv -an

# mov
do_libav mov "-acodec pcm_alaw"

# nut
#do_libav nut "-acodec mp2"

# dv
do_libav dv "-ar 48000 -r 25 -s pal -ac 2"

# gxf
do_libav gxf "-ar 48000 -r 25 -s pal -ac 1"

####################
# streamed images
# mjpeg
#file=${outfile}libav.mjpeg
#do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src $file
#do_ffmpeg_crc $file -i $file

# pbmpipe
do_streamed_images pbm

# pgmpipe
do_streamed_images pgm

# ppmpipe
do_streamed_images ppm

# gif
file=${outfile}libav.gif
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -pix_fmt rgb24 $file
#do_ffmpeg_crc $file -i $file

# yuv4mpeg
file=${outfile}libav.y4m
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src $file
#do_ffmpeg_crc $file -i $file

####################
# image formats
# pgm (we do not do md5 on image files yet)
do_image_formats pgm

# ppm (we do not do md5 on image files yet)
do_image_formats ppm

# jpeg (we do not do md5 on image files yet)
do_image_formats jpg "-flags +bitexact -dct fastint -idct simple -pix_fmt yuvj420p" "-f image2"

####################
# audio only

# wav
do_audio_only wav

# alaw
do_audio_only al

# mulaw
do_audio_only ul

# au
do_audio_only au

# mmf
do_audio_only mmf

# aiff
do_audio_only aif

# voc
do_audio_only voc

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



if $diff_cmd "$logfile" "$reffile" ; then
    echo
    echo Regression test succeeded.
    exit 0
else
    echo
    echo Regression test: Error.
    exit 1
fi
