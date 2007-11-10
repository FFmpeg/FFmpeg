#!/bin/sh
#
# automatic regression test for ffmpeg
#
#
#set -x
# Even in the 21st century some diffs do not support -u.
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

datadir="./tests/data"

logfile="$datadir/ffmpeg.regression"
outfile="$datadir/a-"

# tests to run
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
    do_libavtest=y
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
    do_wma=y
    do_vorbis=y
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
    do_flashsv=y
fi


# various files
ffmpeg="./ffmpeg_g"
tiny_psnr="tests/tiny_psnr"
reffile="$2"
benchfile="$datadir/ffmpeg.bench"
bench="$datadir/bench.tmp"
bench2="$datadir/bench2.tmp"
raw_src="$3/%02d.pgm"
raw_dst="$datadir/out.yuv"
raw_ref="$datadir/ref.yuv"
pcm_src="tests/asynth1.sw"
pcm_dst="$datadir/out.wav"
pcm_ref="$datadir/ref.wav"
if [ X"`echo | md5sum 2> /dev/null`" != X ]; then
    do_md5sum() { md5sum -b $1; }
elif [ -x /sbin/md5 ]; then
    do_md5sum() { /sbin/md5 -r $1 | sed 's# \**\./# *./#'; }
else
    do_md5sum() { echo No md5sum program found; }
fi

# create the data directory if it does not exist
mkdir -p $datadir

FFMPEG_OPTS="-y -flags +bitexact -dct fastint -idct simple"

do_ffmpeg()
{
    f="$1"
    shift
    echo $ffmpeg $FFMPEG_OPTS $*
    $ffmpeg $FFMPEG_OPTS -benchmark $* > $bench 2> /tmp/ffmpeg$$
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
    expr "`cat $bench`" : '.*utime=\(.*s\)' > $bench2
    echo `cat $bench2` $f >> $benchfile
}

do_ffmpeg_nomd5()
{
    f="$1"
    shift
    echo $ffmpeg $FFMPEG_OPTS $*
    $ffmpeg $FFMPEG_OPTS -benchmark $* > $bench 2> /tmp/ffmpeg$$
    egrep -v "^(Stream|Press|Input|Output|frame|  Stream|  Duration|video:)" /tmp/ffmpeg$$ || true
    rm -f /tmp/ffmpeg$$
    if [ $f = $raw_dst ] ; then
        $tiny_psnr $f $raw_ref >> $logfile
    elif [ $f = $pcm_dst ] ; then
        $tiny_psnr $f $pcm_ref 2 >> $logfile
    else
        wc -c $f >> $logfile
    fi
    expr "`cat $bench`" : '.*utime=\(.*s\)' > $bench2
    echo `cat $bench2` $f >> $benchfile
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
    $ffmpeg $FFMPEG_OPTS -benchmark $* > $bench 2> /tmp/ffmpeg$$
    egrep -v "^(Stream|Press|Input|Output|frame|  Stream|  Duration|video:)" /tmp/ffmpeg$$ || true
    rm -f /tmp/ffmpeg$$
    expr "`cat $bench`" : '.*utime=\(.*s\)' > $bench2
    echo `cat $bench2` $f >> $benchfile
}

do_video_decoding()
{
    do_ffmpeg $raw_dst -y $1 -i $file -f rawvideo $2 $raw_dst
}

do_video_encoding()
{
    file=${outfile}$1
    do_ffmpeg $file -y $2 -f $3 -i $raw_src $4 $file
}

do_audio_encoding()
{
    file=${outfile}$1
    do_ffmpeg $file -y -ab 128k -ac 2 -f s16le -i $pcm_src $3 $file
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
    $ffmpeg -t 0.5 -y -qscale 10 -f pgmyuv -i $raw_src $2 $3 -flags +bitexact $file
    do_ffmpeg_crc $file $3 -i $file
    do_md5sum ${outfile}libav02.$1 >> $logfile
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
do_ffmpeg_nocheck $pcm_ref -y -ab 128k -ac 2 -ar 44100 -f s16le -i $pcm_src -f wav $pcm_ref

###################################
if [ -n "$do_mpeg" ] ; then
# mpeg1
do_video_encoding mpeg1.mpg "-qscale 10" pgmyuv "-f mpeg1video"
do_video_decoding
fi

###################################
if [ -n "$do_mpeg2" ] ; then
# mpeg2
do_video_encoding mpeg2.mpg "-qscale 10" pgmyuv "-vcodec mpeg2video -f mpeg1video"
do_video_decoding

# mpeg2 encoding intra vlc qprd
do_video_encoding mpeg2ivlc-qprd.mpg "-vb 500k -bf 2 -flags +trell+qprd+mv0 -flags2 +ivlc -cmp 2 -subcmp 2 -mbd rd" pgmyuv "-vcodec mpeg2video -f mpeg2video"

# mpeg2 decoding
do_video_decoding

# mpeg2
do_video_encoding mpeg2.mpg "-qscale 10" pgmyuv "-vcodec mpeg2video -idct int -dct int -f mpeg1video"
do_video_decoding "-idct int"

# mpeg2 encoding interlaced
do_video_encoding mpeg2i.mpg "-qscale 10" pgmyuv "-vcodec mpeg2video -f mpeg1video -flags +ildct+ilme"

# mpeg2 decoding
do_video_decoding
fi

###################################
if [ -n "$do_mpeg2thread" ] ; then
# mpeg2 encoding interlaced
do_video_encoding mpeg2thread.mpg "-qscale 10" pgmyuv "-vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -threads 2"

# mpeg2 decoding
do_video_decoding

# mpeg2 encoding interlaced using intra vlc
do_video_encoding mpeg2threadivlc.mpg "-qscale 10" pgmyuv "-vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -flags2 +ivlc -threads 2"

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
# msmpeg4
do_video_encoding msmpeg4v2.avi "-qscale 10" pgmyuv "-an -vcodec msmpeg4v2"
do_video_decoding
fi

###################################
if [ -n "$do_msmpeg4" ] ; then
# msmpeg4
do_video_encoding msmpeg4.avi "-qscale 10" pgmyuv "-an -vcodec msmpeg4"
do_video_decoding
fi

###################################
if [ -n "$do_wmv1" ] ; then
# wmv1
do_video_encoding wmv1.avi "-qscale 10" pgmyuv "-an -vcodec wmv1"
do_video_decoding
fi

###################################
if [ -n "$do_wmv2" ] ; then
# wmv2
do_video_encoding wmv2.avi "-qscale 10" pgmyuv "-an -vcodec wmv2"
do_video_decoding
fi

###################################
if [ -n "$do_h261" ] ; then
# h261
do_video_encoding h261.avi "-qscale 11" pgmyuv "-s 352x288 -an -vcodec h261"
do_video_decoding
fi

###################################
if [ -n "$do_h263" ] ; then
# h263
do_video_encoding h263.avi "-qscale 10" pgmyuv "-s 352x288 -an -vcodec h263"
do_video_decoding
fi

###################################
if [ -n "$do_h263p" ] ; then
# h263p
do_video_encoding h263p.avi "-qscale 2 -flags +umv+aiv+aic" pgmyuv "-s 352x288 -an -vcodec h263p -ps 300"
do_video_decoding
fi

###################################
if [ -n "$do_mpeg4" ] ; then
# mpeg4
do_video_encoding odivx.mp4 "-flags +mv4 -mbd bits -qscale 10" pgmyuv "-an -vcodec mpeg4"
do_video_decoding
fi

###################################
if [ -n "$do_huffyuv" ] ; then
# huffyuv
do_video_encoding huffyuv.avi "" pgmyuv "-an -vcodec huffyuv -pix_fmt yuv422p"
do_video_decoding "" "-strict -2 -pix_fmt yuv420p"
fi

###################################
if [ -n "$do_rc" ] ; then
# mpeg4 rate control
do_video_encoding mpeg4-rc.avi "-b 400k -bf 2" pgmyuv "-an -vcodec mpeg4"
do_video_decoding
fi

###################################
if [ -n "$do_mpeg4adv" ] ; then
# mpeg4
do_video_encoding mpeg4-adv.avi "-qscale 9 -flags +mv4+part+aic+trell -mbd bits -ps 200" pgmyuv "-an -vcodec mpeg4"
do_video_decoding

# mpeg4
do_video_encoding mpeg4-qprd.avi "-b 450k -bf 2 -flags +mv4+trell+qprd+mv0 -cmp 2 -subcmp 2 -mbd rd" pgmyuv "-an -vcodec mpeg4"
do_video_decoding

# mpeg4
do_video_encoding mpeg4-adap.avi "-b 550k -bf 2 -flags +mv4+trell+mv0 -cmp 1 -subcmp 2 -mbd rd -scplx_mask 0.3" pgmyuv "-an -vcodec mpeg4"
do_video_decoding

# mpeg4
do_video_encoding mpeg4-Q.avi "-qscale 7 -flags +mv4+qpel -mbd 2 -bf 2 -cmp 1 -subcmp 2" pgmyuv "-an -vcodec mpeg4"
do_video_decoding
fi

###################################
if [ -n "$do_mpeg4thread" ] ; then
# mpeg4
do_video_encoding mpeg4-thread.avi "-b 500k -flags +mv4+part+aic+trell -mbd bits -ps 200 -bf 2" pgmyuv "-an -vcodec mpeg4 -threads 2"
do_video_decoding
fi

###################################
if [ -n "$do_mp4psp" ] ; then
# mp4 PSP style
do_video_encoding mpeg4-PSP.mp4 "-vb 768k -s 320x240" psp "-ar 24000 -ab 32k -i $raw_src"
fi

###################################
if [ -n "$do_error" ] ; then
# damaged mpeg4
do_video_encoding error-mpeg4-adv.avi "-qscale 7 -flags +mv4+part+aic -mbd rd -ps 250 -error 10" pgmyuv "-an -vcodec mpeg4"
do_video_decoding
fi

###################################
if [ -n "$do_mpeg4nr" ] ; then
# noise reduction
do_video_encoding mpeg4-nr.avi "-qscale 8 -flags +mv4 -mbd rd -nr 200" pgmyuv "-an -vcodec mpeg4"
do_video_decoding
fi

###################################
if [ -n "$do_mpeg1b" ] ; then
# mpeg1
do_video_encoding mpeg1b.mpg "-qscale 8 -bf 3 -ps 200" pgmyuv "-an -vcodec mpeg1video -f mpeg1video"
do_video_decoding
fi

###################################
if [ -n "$do_mjpeg" ] ; then
# mjpeg
do_video_encoding mjpeg.avi "-qscale 10" pgmyuv "-an -vcodec mjpeg -pix_fmt yuvj420p"
do_video_decoding "" "-pix_fmt yuv420p"
fi

###################################
if [ -n "$do_ljpeg" ] ; then
# ljpeg
do_video_encoding ljpeg.avi "" pgmyuv "-an -vcodec ljpeg -strict -1"
do_video_decoding
fi

###################################
if [ -n "$do_jpegls" ] ; then
# jpeg ls
do_video_encoding jpegls.avi "" pgmyuv "-an -vcodec jpegls -vtag MJPG"
do_video_decoding "" "-pix_fmt yuv420p"
fi

###################################
if [ -n "$do_rv10" ] ; then
# rv10 encoding
do_video_encoding rv10.rm "-qscale 10" pgmyuv "-an"
do_video_decoding
fi

###################################
if [ -n "$do_rv20" ] ; then
# rv20 encoding
do_video_encoding rv20.rm "-qscale 10" pgmyuv "-vcodec rv20 -an"
do_video_decoding
fi

###################################
if [ -n "$do_asv1" ] ; then
# asv1 encoding
do_video_encoding asv1.avi "-qscale 10" pgmyuv "-an -vcodec asv1"
do_video_decoding
fi

###################################
if [ -n "$do_asv2" ] ; then
# asv2 encoding
do_video_encoding asv2.avi "-qscale 10" pgmyuv "-an -vcodec asv2"
do_video_decoding
fi

###################################
if [ -n "$do_flv" ] ; then
# flv encoding
do_video_encoding flv.flv "-qscale 10" pgmyuv "-an -vcodec flv"
do_video_decoding
fi

###################################
if [ -n "$do_ffv1" ] ; then
# ffv1 encoding
do_video_encoding ffv1.avi "-strict -2" pgmyuv "-an -vcodec ffv1"
do_video_decoding
fi

###################################
if [ -n "$do_snow" ] ; then
# snow
do_video_encoding snow.avi "-strict -2" pgmyuv "-an -vcodec snow -qscale 2 -flags +qpel -me iter -dia_size 2 -cmp 12 -subcmp 12 -s 128x64"
do_video_decoding "" "-s 352x288"
fi

###################################
if [ -n "$do_snowll" ] ; then
# snow
do_video_encoding snow53.avi "-strict -2" pgmyuv "-an -vcodec snow -qscale .001 -pred 1 -flags +mv4+qpel"
do_video_decoding
fi

###################################
if [ -n "$do_dv" ] ; then
# dv
do_video_encoding dv.dv "-dct int" pgmyuv "-s pal -an"
do_video_decoding "" "-s cif"
fi

###################################
if [ -n "$do_dv50" ] ; then
# dv50
do_video_encoding dv.dv "-dct int" pgmyuv "-s pal -pix_fmt yuv422p -an"
do_video_decoding "" "-s cif -pix_fmt yuv420p"
fi


###################################
if [ -n "$do_svq1" ] ; then
# svq1
do_video_encoding svq1.mov "" pgmyuv "-an -vcodec svq1 -qscale 3 -pix_fmt yuv410p"
do_video_decoding "" "-pix_fmt yuv420p"
fi

###################################
if [ -n "$do_flashsv" ] ; then
# svq1
do_video_encoding flashsv.flv "" pgmyuv "-an -vcodec flashsv "
do_video_decoding "" "-pix_fmt yuv420p"
fi

###################################
if [ -n "$do_mp2" ] ; then
# mp2
do_audio_encoding mp2.mp2 "-ar 44100"
do_audio_decoding
$tiny_psnr $pcm_dst $pcm_ref 2 1924 >> $logfile
fi

###################################
if [ -n "$do_ac3" ] ; then
# ac3
do_audio_encoding ac3.rm "" -vn
#do_audio_decoding
fi

###################################
if [ -n "$do_g726" ] ; then
# g726
do_audio_encoding g726.wav "-ar 44100" "-ab 32k -ac 1 -ar 8000 -acodec g726"
do_audio_decoding
fi

###################################
if [ -n "$do_adpcm_ima_wav" ] ; then
# adpcm ima
do_audio_encoding adpcm_ima.wav "-ar 44100" "-acodec adpcm_ima_wav"
do_audio_decoding
fi

###################################
if [ -n "$do_adpcm_ms" ] ; then
# adpcm ms
do_audio_encoding adpcm_ms.wav "-ar 44100" "-acodec adpcm_ms"
do_audio_decoding
fi

###################################
if [ -n "$do_adpcm_yam" ] ; then
# adpcm yamaha
do_audio_encoding adpcm_yam.wav "-ar 44100" "-acodec adpcm_yamaha"
do_audio_decoding
fi

###################################
if [ -n "$do_flac" ] ; then
# flac
do_audio_encoding flac.flac "-ar 44100" "-acodec flac -compression_level 2"
do_audio_decoding
fi

###################################
if [ -n "$do_wma" ] ; then
# wmav1
do_audio_encoding wmav1.asf "-ar 44100" "-acodec wmav1"
do_ffmpeg_nomd5 $pcm_dst -y -i $file -f wav $pcm_dst
$tiny_psnr $pcm_dst $pcm_ref 2 8192 >> $logfile
# wmav2
do_audio_encoding wmav2.asf "-ar 44100" "-acodec wmav2"
do_ffmpeg_nomd5 $pcm_dst -y -i $file -f wav $pcm_dst
$tiny_psnr $pcm_dst $pcm_ref 2 8192 >> $logfile
fi

###################################
#if [ -n "$do_vorbis" ] ; then
# vorbis
#disabled because it is broken
#do_audio_encoding vorbis.asf "-ar 44100" "-acodec vorbis"
#do_audio_decoding
#fi

###################################
# libavformat testing
###################################

if [ -n "$do_libavtest" ] ; then

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

# swf
do_libav swf -an

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

# nut
do_libav nut "-acodec mp2"

# mkv
do_libav mkv


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
# pgm
do_image_formats pgm

# ppm
do_image_formats ppm

# bmp
do_image_formats bmp

# tga
do_image_formats tga

# tiff
do_image_formats tiff "-pix_fmt rgb24"

# sgi
do_image_formats sgi

# jpeg
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

# ogg
do_audio_only ogg

####################
# pix_fmt conversions
conversions="yuv420p yuv422p yuv444p yuyv422 yuv410p yuv411p yuvj420p \
             yuvj422p yuvj444p rgb24 bgr24 rgb32 rgb565 rgb555 gray monow \
             monob pal8 yuv440p yuvj440p"
for pix_fmt in $conversions ; do
    file=${outfile}libav-${pix_fmt}.yuv
    do_ffmpeg_nocheck $file -r 1 -t 1 -y -f pgmyuv -i $raw_src \
                            -f rawvideo -s 352x288 -pix_fmt $pix_fmt $raw_dst
    do_ffmpeg $file -f rawvideo -s 352x288 -pix_fmt $pix_fmt -i $raw_dst \
                    -f rawvideo -s 352x288 -pix_fmt yuv444p $file
done

fi #  [ -n "$do_libavtest" ]



if $diff_cmd "$logfile" "$reffile" ; then
    echo
    echo Regression test succeeded.
    exit 0
else
    echo
    echo Regression test: Error.
    exit 1
fi
