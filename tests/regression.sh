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

do_ffmpeg()
{
    f="$1"
    shift
    echo $ffmpeg -y -flags +bitexact -dct fastint -idct simple $*
    $ffmpeg -y -flags +bitexact -dct fastint -idct simple -benchmark $* > $datadir/bench.tmp 2> /tmp/ffmpeg$$
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
    echo $ffmpeg -y -flags +bitexact -dct fastint -idct simple $* -f crc $datadir/ffmpeg.crc
    $ffmpeg -y -flags +bitexact -dct fastint -idct simple $* -f crc $datadir/ffmpeg.crc > /tmp/ffmpeg$$ 2>&1
    egrep -v "^(Stream|Press|Input|Output|frame|  Stream|  Duration|video:|ffmpeg version|  configuration|  built)" /tmp/ffmpeg$$ || true
    rm -f /tmp/ffmpeg$$
    echo "$f `cat $datadir/ffmpeg.crc`" >> $logfile
}

do_ffmpeg_nocheck()
{
    f="$1"
    shift
    echo $ffmpeg -y -flags +bitexact -dct fastint -idct simple $*
    $ffmpeg -y -flags +bitexact -dct fastint -idct simple -benchmark $* > $datadir/bench.tmp 2> /tmp/ffmpeg$$
    egrep -v "^(Stream|Press|Input|Output|frame|  Stream|  Duration|video:)" /tmp/ffmpeg$$ || true
    rm -f /tmp/ffmpeg$$
    expr "`cat $datadir/bench.tmp`" : '.*utime=\(.*s\)' > $datadir/bench2.tmp
    echo `cat $datadir/bench2.tmp` $f >> $benchfile
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
file=${outfile}mpeg1.mpg
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -f mpeg1video $file

# mpeg1 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_mpeg2" ] ; then
# mpeg2 encoding
file=${outfile}mpeg2.mpg
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -vcodec mpeg2video -f mpeg1video $file

# mpeg2 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst

# mpeg2 encoding using intra vlc
file=${outfile}mpeg2ivlc.mpg
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -vcodec mpeg2video -f mpeg1video -flags2 +ivlc $file

# mpeg2 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst

# mpeg2 encoding
file=${outfile}mpeg2.mpg
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -vcodec mpeg2video -idct int -dct int -f mpeg1video $file

# mpeg2 decoding
do_ffmpeg $raw_dst -y -idct int -i $file -f rawvideo $raw_dst

# mpeg2 encoding interlaced
file=${outfile}mpeg2i.mpg
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -vcodec mpeg2video -f mpeg1video -flags +ildct+ilme $file

# mpeg2 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_mpeg2thread" ] ; then
# mpeg2 encoding interlaced
file=${outfile}mpeg2thread.mpg
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -threads 2 $file

# mpeg2 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst

# mpeg2 encoding interlaced using intra vlc
file=${outfile}mpeg2threadivlc.mpg
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -flags2 +ivlc -threads 2 $file

# mpeg2 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst

# mpeg2 encoding interlaced
file=${outfile}mpeg2reuse.mpg
do_ffmpeg $file -y -sameq -me_threshold 256 -mb_threshold 1024 -i ${outfile}mpeg2thread.mpg -vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -threads 4 $file

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
if [ -n "$do_h261" ] ; then
# h261 encoding
file=${outfile}h261.avi
do_ffmpeg $file -y -qscale 11 -f pgmyuv -i $raw_src -s 352x288 -an -vcodec h261 $file

# h261 decoding
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
do_ffmpeg $file -y -qscale 2 -flags +umv+aiv+aic -f pgmyuv -i $raw_src -s 352x288 -an -vcodec h263p -ps 300 $file

# h263p decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_mpeg4" ] ; then
# mpeg4
file=${outfile}odivx.mp4
do_ffmpeg $file -y -flags +mv4 -mbd bits -qscale 10 -f pgmyuv -i $raw_src -an -vcodec mpeg4 $file

# mpeg4 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_huffyuv" ] ; then
# huffyuv
file=${outfile}huffyuv.avi
do_ffmpeg $file -y -f pgmyuv -i $raw_src -an -vcodec huffyuv -pix_fmt yuv422p $file

# huffyuv decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo -strict -2 -pix_fmt yuv420p $raw_dst
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
do_ffmpeg $file -y -qscale 9 -flags +mv4+part+aic+trell -mbd bits -ps 200 -f pgmyuv -i $raw_src -an -vcodec mpeg4 $file

# mpeg4 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_mpeg4thread" ] ; then
# mpeg4
file=${outfile}mpeg4-thread.avi
do_ffmpeg $file -y -b 500 -flags +mv4+part+aic+trell -mbd bits  -ps 200 -bf 2 -f pgmyuv -i $raw_src -an -vcodec mpeg4 -threads 2 $file

# mpeg4 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_mpeg4adv" ] ; then
# mpeg4
file=${outfile}mpeg4-Q.avi
do_ffmpeg $file -y -qscale 7 -flags +mv4+qpel -mbd 2 -bf 2 -cmp 1 -subcmp 2 -f pgmyuv -i $raw_src -an -vcodec mpeg4 $file

# mpeg4 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_mp4psp" ] ; then
# mp4 PSP style
file=${outfile}mpeg4-PSP.mp4
do_ffmpeg $file -y -b 768 -s 320x240 -f psp -ar 24000 -ab 32 -i $raw_src $file
fi

###################################
if [ -n "$do_error" ] ; then
# damaged mpeg4
file=${outfile}error-mpeg4-adv.avi
do_ffmpeg $file -y -qscale 7 -flags +mv4+part+aic -mbd rd -ps 250 -error 10 -f pgmyuv -i $raw_src -an -vcodec mpeg4 $file

# damaged mpeg4 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_mpeg4nr" ] ; then
# noise reduction
file=${outfile}mpeg4-nr.avi
do_ffmpeg $file -y -qscale 8 -flags +mv4 -mbd rd -nr 200 -f pgmyuv -i $raw_src -an -vcodec mpeg4 $file

# mpeg4 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_mpeg1b" ] ; then
# mpeg1
file=${outfile}mpeg1b.mpg
do_ffmpeg $file -y -qscale 8 -bf 3 -ps 200 -f pgmyuv -i $raw_src -an -vcodec mpeg1video -f mpeg1video $file

# mpeg1 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_mjpeg" ] ; then
# mjpeg
file=${outfile}mjpeg.avi
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -an -vcodec mjpeg -pix_fmt yuvj420p $file

# mjpeg decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo -pix_fmt yuv420p $raw_dst
fi

###################################
if [ -n "$do_ljpeg" ] ; then
# ljpeg
file=${outfile}ljpeg.avi
do_ffmpeg $file -y -f pgmyuv -i $raw_src -an -vcodec ljpeg -strict -1 $file

# ljpeg decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_jpegls" ] ; then
# jpeg ls
file=${outfile}jpegls.avi
do_ffmpeg $file -y -f pgmyuv -i $raw_src -an -vcodec jpegls -vtag MJPG $file

# jpeg ls decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo -pix_fmt yuv420p $raw_dst
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
if [ -n "$do_rv20" ] ; then
# rv20 encoding
file=${outfile}rv20.rm
do_ffmpeg $file -y -qscale 10 -f pgmyuv -i $raw_src -vcodec rv20 -an $file

# rv20 decoding
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
do_ffmpeg $file -y -strict -2 -f pgmyuv -i $raw_src -an -vcodec ffv1 $file

# ffv1 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_snow" ] ; then
# snow encoding
file=${outfile}snow.avi
do_ffmpeg $file -y -strict -2 -f pgmyuv -i $raw_src -an -vcodec snow -qscale 2 -flags +qpel -me iter -dia_size 2 -cmp 12 -subcmp 12 -s 128x64 $file

# snow decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo -s 352x288 $raw_dst
fi

###################################
if [ -n "$do_snowll" ] ; then
# snow encoding
file=${outfile}snow53.avi
do_ffmpeg $file -y -strict -2 -f pgmyuv -i $raw_src -an -vcodec snow -qscale .001 -pred 1 -flags +mv4+qpel $file

# snow decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo $raw_dst
fi

###################################
if [ -n "$do_dv" ] ; then
# dv encoding
file=${outfile}dv.dv
do_ffmpeg $file -dct int -y -f pgmyuv -i $raw_src -s pal -an $file

# dv decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo -s cif $raw_dst
fi

###################################
if [ -n "$do_dv50" ] ; then
# dv50 encoding
file=${outfile}dv.dv
do_ffmpeg $file -dct int -y -f pgmyuv -i $raw_src -s pal -pix_fmt yuv422p -an $file

# dv50 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo -s cif -pix_fmt yuv420p $raw_dst
fi


###################################
if [ -n "$do_svq1" ] ; then
# svq1 encoding
file=${outfile}svq1.mov
do_ffmpeg $file -y -f pgmyuv -i $raw_src -an -vcodec svq1 -qscale 3 -pix_fmt yuv410p $file

# svq1 decoding
do_ffmpeg $raw_dst -y -i $file -f rawvideo -pix_fmt yuv420p $raw_dst
fi

###################################
if [ -n "$do_mp2" ] ; then
# mp2 encoding
file=${outfile}mp2.mp2
do_ffmpeg $file -y -ab 128 -ac 2 -ar 44100 -f s16le -i $pcm_src $file

# mp2 decoding
do_ffmpeg $pcm_dst -y -i $file -f wav $pcm_dst
$tiny_psnr $pcm_dst $pcm_ref 2 1924 >> $logfile
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
if [ -n "$do_g726" ] ; then
# g726 encoding
file=${outfile}g726.wav
do_ffmpeg $file -y -ab 128 -ac 2 -ar 44100 -f s16le -i $pcm_src -ab 32 -ac 1 -ar 8000 -acodec g726 $file

# g726 decoding
do_ffmpeg $pcm_dst -y -i $file -f wav $pcm_dst
fi

###################################
if [ -n "$do_adpcm_ima_wav" ] ; then
# encoding
file=${outfile}adpcm_ima.wav
do_ffmpeg $file -y -ab 128 -ac 2 -ar 44100 -f s16le -i $pcm_src -acodec adpcm_ima_wav $file

# decoding
do_ffmpeg $pcm_dst -y -i $file -f wav $pcm_dst
fi

###################################
if [ -n "$do_adpcm_ms" ] ; then
# encoding
file=${outfile}adpcm_ms.wav
do_ffmpeg $file -y -ab 128 -ac 2 -ar 44100 -f s16le -i $pcm_src -acodec adpcm_ms $file

# decoding
do_ffmpeg $pcm_dst -y -i $file -f wav $pcm_dst
fi

###################################
if [ -n "$do_adpcm_yam" ] ; then
# encoding
file=${outfile}adpcm_yam.wav
do_ffmpeg $file -y -ab 128 -ac 2 -ar 44100 -f s16le -i $pcm_src -acodec adpcm_yamaha $file

# decoding
do_ffmpeg $pcm_dst -y -i $file -f wav $pcm_dst
fi

###################################
if [ -n "$do_flac" ] ; then
# encoding
file=${outfile}flac.flac
do_ffmpeg $file -y -ab 128 -ac 2 -ar 44100 -f s16le -i $pcm_src -acodec flac -compression_level 2 $file

# decoding
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
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src -acodec mp2 $file
do_ffmpeg_crc $file -i $file -r 25

# rm
file=${outfile}libav.rm
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
# broken
#do_ffmpeg_crc $file -i $file

# mpegps
file=${outfile}libav.mpg
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# mpegts
file=${outfile}libav.ts
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# swf (decode audio only)
file=${outfile}libav.swf
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src -acodec mp2 $file
do_ffmpeg_crc $file -i $file

# ffm
file=${outfile}libav.ffm
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# flv
file=${outfile}libav.flv
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src -an $file
do_ffmpeg_crc $file -i $file

# mov
file=${outfile}libav.mov
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src -acodec pcm_alaw $file
do_ffmpeg_crc $file -i $file

# nut
file=${outfile}libav.nut
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src -acodec mp2 $file
do_ffmpeg_crc $file -i $file

# dv
file=${outfile}libav.dv
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f s16le -i $pcm_src -ar 48000 -r 25 -s pal -ac 2 $file
do_ffmpeg_crc $file -i $file

####################
# streamed images
# mjpeg
#file=${outfile}libav.mjpeg
#do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src $file
#do_ffmpeg_crc $file -i $file

# pbmpipe
file=${outfile}libav.pbm
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f image2pipe $file
do_ffmpeg_crc $file -f image2pipe -i $file

# pgmpipe
file=${outfile}libav.pgm
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f image2pipe $file
do_ffmpeg_crc $file -f image2pipe -i $file

# ppmpipe
file=${outfile}libav.ppm
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src -f image2pipe $file
do_ffmpeg_crc $file -f image2pipe -i $file

# gif
file=${outfile}libav.gif
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src $file
#do_ffmpeg_crc $file -i $file

# yuv4mpeg
file=${outfile}libav.y4m
do_ffmpeg $file -t 1 -y -qscale 10 -f pgmyuv -i $raw_src $file
#do_ffmpeg_crc $file -i $file

####################
# image formats
# pgm (we do not do md5 on image files yet)
file=${outfile}libav%02d.pgm
$ffmpeg -t 0.5 -y -qscale 10 -f pgmyuv -i $raw_src $file
do_ffmpeg_crc $file -i $file

# ppm (we do not do md5 on image files yet)
file=${outfile}libav%02d.ppm
$ffmpeg -t 0.5 -y -qscale 10 -f pgmyuv -i $raw_src $file
do_ffmpeg_crc $file -i $file

# jpeg (we do not do md5 on image files yet)
file=${outfile}libav%02d.jpg
$ffmpeg -t 0.5 -y -qscale 10 -f pgmyuv -i $raw_src -flags +bitexact -dct fastint -idct simple -pix_fmt yuvj420p -f image2 $file
do_ffmpeg_crc $file -f image2 -i $file

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

# mmf
file=${outfile}libav.mmf
do_ffmpeg $file -t 1 -y -qscale 10 -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# aiff
file=${outfile}libav.aif
do_ffmpeg $file -t 1 -y -qscale 10 -f s16le -i $pcm_src $file
do_ffmpeg_crc $file -i $file

# voc
file=${outfile}libav.voc
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



if $diff_cmd "$logfile" "$reffile" ; then
    echo
    echo Regression test succeeded.
    exit 0
else
    echo
    echo Regression test: Error.
    exit 1
fi
