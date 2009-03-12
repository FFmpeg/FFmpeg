#!/bin/sh
#
# automatic regression test for ffmpeg
#
#
#set -x

set -e

target_exec=$5
target_path=$6

datadir="./tests/data"
target_datadir="${target_path}/${datadir}"

test="${1#regtest-}"
this="$test.$2"
logfile="$datadir/$this.regression"
outfile="$datadir/$4-"

eval do_$test=y

# various files
ffmpeg="$target_exec ${target_path}/ffmpeg_g"
tiny_psnr="tests/tiny_psnr"
benchfile="$datadir/$this.bench"
bench="$datadir/$this.bench.tmp"
bench2="$datadir/$this.bench2.tmp"
raw_src="${target_path}/$3/%02d.pgm"
raw_dst="$datadir/$this.out.yuv"
raw_ref="$datadir/$2.ref.yuv"
pcm_src="${target_path}/tests/asynth1.sw"
pcm_dst="$datadir/$this.out.wav"
pcm_ref="$datadir/$2.ref.wav"
crcfile="$datadir/$this.crc"
target_crcfile="$target_datadir/$this.crc"

if [ X"`echo | md5sum 2> /dev/null`" != X ]; then
    do_md5sum() { md5sum -b $1; }
elif [ X"`echo | md5 2> /dev/null`" != X ]; then
    do_md5sum() { md5 -r $1 | sed 's# \**\./# *./#'; }
elif [ -x /sbin/md5 ]; then
    do_md5sum() { /sbin/md5 -r $1 | sed 's# \**\./# *./#'; }
else
    do_md5sum() { echo No md5sum program found; }
fi

# create the data directory if it does not exist
mkdir -p $datadir

FFMPEG_OPTS="-y -flags +bitexact -dct fastint -idct simple -sws_flags +accurate_rnd+bitexact"

do_ffmpeg()
{
    f="$1"
    shift
    set -- $* ${target_path}/$f
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
    set -- $* ${target_path}/$f
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
    echo $ffmpeg $FFMPEG_OPTS $* -f crc "$target_crcfile"
    $ffmpeg $FFMPEG_OPTS $* -f crc "$target_crcfile" > /tmp/ffmpeg$$ 2>&1
    egrep -v "^(Stream|Press|Input|Output|frame|  Stream|  Duration|video:|ffmpeg version|  configuration|  built)" /tmp/ffmpeg$$ || true
    rm -f /tmp/ffmpeg$$
    echo "$f `cat $crcfile`" >> $logfile
    rm -f "$crcfile"
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
    do_ffmpeg $raw_dst $1 -i $target_path/$file -f rawvideo $2
    rm -f $raw_dst
}

do_video_encoding()
{
    file=${outfile}$1
    do_ffmpeg $file $2 -f image2 -vcodec pgmyuv -i $raw_src $3
}

do_audio_encoding()
{
    file=${outfile}$1
    do_ffmpeg $file -ab 128k -ac 2 -f s16le -i $pcm_src $3
}

do_audio_decoding()
{
    do_ffmpeg $pcm_dst -i $target_path/$file -sample_fmt s16 -f wav
}

do_libav()
{
    file=${outfile}libav.$1
    do_ffmpeg $file -t 1 -qscale 10 -f image2 -vcodec pgmyuv -i $raw_src -f s16le -i $pcm_src $2
    do_ffmpeg_crc $file -i $target_path/$file $3
}

do_streamed_images()
{
    file=${outfile}${1}pipe.$1
    do_ffmpeg $file -t 1 -qscale 10 -f image2 -vcodec pgmyuv -i $raw_src -f image2pipe
    do_ffmpeg_crc $file -f image2pipe -i $target_path/$file
}

do_image_formats()
{
    file=${outfile}libav%02d.$1
    $ffmpeg -t 0.5 -y -qscale 10 -f image2 -vcodec pgmyuv -i $raw_src $2 $3 -flags +bitexact -sws_flags +accurate_rnd+bitexact $target_path/$file
    do_md5sum ${outfile}libav02.$1 >> $logfile
    do_ffmpeg_crc $file $3 -i $target_path/$file
    wc -c ${outfile}libav02.$1 >> $logfile
}

do_audio_only()
{
    file=${outfile}libav.$1
    do_ffmpeg $file -t 1 -qscale 10 -f s16le -i $pcm_src
    do_ffmpeg_crc $file -i $target_path/$file
}

rm -f "$logfile"
rm -f "$benchfile"

# generate reference for quality check
if [ -n "$do_ref" ]; then
do_ffmpeg_nocheck $raw_ref -f image2 -vcodec pgmyuv -i $raw_src -an -f rawvideo $target_path/$raw_ref
do_ffmpeg_nocheck $pcm_ref -ab 128k -ac 2 -ar 44100 -f s16le -i $pcm_src -f wav $target_path/$pcm_ref
fi

if [ -n "$do_mpeg" ] ; then
# mpeg1
do_video_encoding mpeg1.mpg "-qscale 10" "-f mpeg1video"
do_video_decoding
fi

if [ -n "$do_mpeg2" ] ; then
# mpeg2
do_video_encoding mpeg2.mpg "-qscale 10" "-vcodec mpeg2video -f mpeg1video"
do_video_decoding

# mpeg2 encoding intra vlc qprd
do_video_encoding mpeg2ivlc-qprd.mpg "-vb 500k -bf 2 -trellis 1 -flags +qprd+mv0 -flags2 +ivlc -cmp 2 -subcmp 2 -mbd rd" "-vcodec mpeg2video -f mpeg2video"
do_video_decoding

#mpeg2 4:2:2 encoding
do_video_encoding mpeg2_422.mpg "-vb 1000k -bf 2 -trellis 1 -flags +qprd+mv0+ildct+ilme -flags2 +ivlc -mbd rd" "-vcodec mpeg2video -pix_fmt yuv422p -f mpeg2video"
do_video_decoding

# mpeg2
do_video_encoding mpeg2.mpg "-qscale 10" "-vcodec mpeg2video -idct int -dct int -f mpeg1video"
do_video_decoding "-idct int"

# mpeg2 encoding interlaced
do_video_encoding mpeg2i.mpg "-qscale 10" "-vcodec mpeg2video -f mpeg1video -flags +ildct+ilme"
do_video_decoding
fi

if [ -n "$do_mpeg2thread" ] ; then
# mpeg2 encoding interlaced
do_video_encoding mpeg2thread.mpg "-qscale 10" "-vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -threads 2"
do_video_decoding

# mpeg2 encoding interlaced using intra vlc
do_video_encoding mpeg2threadivlc.mpg "-qscale 10" "-vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -flags2 +ivlc -threads 2"
do_video_decoding

# mpeg2 encoding interlaced
file=${outfile}mpeg2reuse.mpg
do_ffmpeg $file -sameq -me_threshold 256 -mb_threshold 1024 -i ${target_path}/${outfile}mpeg2thread.mpg -vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -threads 4
do_video_decoding
fi

if [ -n "$do_msmpeg4v2" ] ; then
do_video_encoding msmpeg4v2.avi "-qscale 10" "-an -vcodec msmpeg4v2"
do_video_decoding
fi

if [ -n "$do_msmpeg4" ] ; then
do_video_encoding msmpeg4.avi "-qscale 10" "-an -vcodec msmpeg4"
do_video_decoding
fi

if [ -n "$do_wmv1" ] ; then
do_video_encoding wmv1.avi "-qscale 10" "-an -vcodec wmv1"
do_video_decoding
fi

if [ -n "$do_wmv2" ] ; then
do_video_encoding wmv2.avi "-qscale 10" "-an -vcodec wmv2"
do_video_decoding
fi

if [ -n "$do_h261" ] ; then
do_video_encoding h261.avi "-qscale 11" "-s 352x288 -an -vcodec h261"
do_video_decoding
fi

if [ -n "$do_h263" ] ; then
do_video_encoding h263.avi "-qscale 10" "-s 352x288 -an -vcodec h263"
do_video_decoding
fi

if [ -n "$do_h263p" ] ; then
do_video_encoding h263p.avi "-qscale 2 -flags +umv+aiv+aic" "-s 352x288 -an -vcodec h263p -ps 300"
do_video_decoding
fi

if [ -n "$do_mpeg4" ] ; then
do_video_encoding odivx.mp4 "-flags +mv4 -mbd bits -qscale 10" "-an -vcodec mpeg4"
do_video_decoding
fi

if [ -n "$do_huffyuv" ] ; then
do_video_encoding huffyuv.avi "" "-an -vcodec huffyuv -pix_fmt yuv422p -sws_flags neighbor+bitexact"
do_video_decoding "" "-strict -2 -pix_fmt yuv420p -sws_flags neighbor+bitexact"
fi

if [ -n "$do_rc" ] ; then
do_video_encoding mpeg4-rc.avi "-b 400k -bf 2" "-an -vcodec mpeg4"
do_video_decoding
fi

if [ -n "$do_mpeg4adv" ] ; then
do_video_encoding mpeg4-adv.avi "-qscale 9 -flags +mv4+part+aic -trellis 1 -mbd bits -ps 200" "-an -vcodec mpeg4"
do_video_decoding

do_video_encoding mpeg4-qprd.avi "-b 450k -bf 2 -trellis 1 -flags +mv4+qprd+mv0 -cmp 2 -subcmp 2 -mbd rd" "-an -vcodec mpeg4"
do_video_decoding

do_video_encoding mpeg4-adap.avi "-b 550k -bf 2 -flags +mv4+mv0 -trellis 1 -cmp 1 -subcmp 2 -mbd rd -scplx_mask 0.3" "-an -vcodec mpeg4"
do_video_decoding

do_video_encoding mpeg4-Q.avi "-qscale 7 -flags +mv4+qpel -mbd 2 -bf 2 -cmp 1 -subcmp 2" "-an -vcodec mpeg4"
do_video_decoding
fi

if [ -n "$do_mpeg4thread" ] ; then
do_video_encoding mpeg4-thread.avi "-b 500k -flags +mv4+part+aic -trellis 1 -mbd bits -ps 200 -bf 2" "-an -vcodec mpeg4 -threads 2"
do_video_decoding
fi

if [ -n "$do_error" ] ; then
do_video_encoding error-mpeg4-adv.avi "-qscale 7 -flags +mv4+part+aic -mbd rd -ps 250 -error 10" "-an -vcodec mpeg4"
do_video_decoding
fi

if [ -n "$do_mpeg4nr" ] ; then
do_video_encoding mpeg4-nr.avi "-qscale 8 -flags +mv4 -mbd rd -nr 200" "-an -vcodec mpeg4"
do_video_decoding
fi

if [ -n "$do_mpeg1b" ] ; then
do_video_encoding mpeg1b.mpg "-qscale 8 -bf 3 -ps 200" "-an -vcodec mpeg1video -f mpeg1video"
do_video_decoding
fi

if [ -n "$do_mjpeg" ] ; then
do_video_encoding mjpeg.avi "-qscale 10" "-an -vcodec mjpeg -pix_fmt yuvj420p"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_ljpeg" ] ; then
do_video_encoding ljpeg.avi "" "-an -vcodec ljpeg -strict -1"
do_video_decoding
fi

if [ -n "$do_jpegls" ] ; then
do_video_encoding jpegls.avi "" "-an -vcodec jpegls -vtag MJPG -sws_flags neighbor+full_chroma_int+accurate_rnd+bitexact"
do_video_decoding "" "-pix_fmt yuv420p  -sws_flags area+bitexact"
fi

if [ -n "$do_rv10" ] ; then
do_video_encoding rv10.rm "-qscale 10" "-an"
do_video_decoding
fi

if [ -n "$do_rv20" ] ; then
do_video_encoding rv20.rm "-qscale 10" "-vcodec rv20 -an"
do_video_decoding
fi

if [ -n "$do_asv1" ] ; then
do_video_encoding asv1.avi "-qscale 10" "-an -vcodec asv1"
do_video_decoding
fi

if [ -n "$do_asv2" ] ; then
do_video_encoding asv2.avi "-qscale 10" "-an -vcodec asv2"
do_video_decoding
fi

if [ -n "$do_flv" ] ; then
do_video_encoding flv.flv "-qscale 10" "-an -vcodec flv"
do_video_decoding
fi

if [ -n "$do_ffv1" ] ; then
do_video_encoding ffv1.avi "-strict -2" "-an -vcodec ffv1"
do_video_decoding
fi

if [ -n "$do_snow" ] ; then
do_video_encoding snow.avi "-strict -2" "-an -vcodec snow -qscale 2 -flags +qpel -me_method iter -dia_size 2 -cmp 12 -subcmp 12 -s 128x64"
do_video_decoding "" "-s 352x288"
fi

if [ -n "$do_snowll" ] ; then
do_video_encoding snow53.avi "-strict -2" "-an -vcodec snow -qscale .001 -pred 1 -flags +mv4+qpel"
do_video_decoding
fi

if [ -n "$do_dv" ] ; then
do_video_encoding dv.dv "-dct int" "-s pal -an"
do_video_decoding "" "-s cif"

do_video_encoding dv411.dv "-dct int" "-s pal -an -pix_fmt yuv411p -sws_flags area+accurate_rnd+bitexact"
do_video_decoding "" "-s cif -sws_flags area+accurate_rnd+bitexact"
fi

if [ -n "$do_dv50" ] ; then
do_video_encoding dv50.dv "-dct int" "-s pal -pix_fmt yuv422p -an -sws_flags neighbor+bitexact"
do_video_decoding "" "-s cif -pix_fmt yuv420p -sws_flags neighbor+bitexact"
fi

if [ -n "$do_svq1" ] ; then
do_video_encoding svq1.mov "" "-an -vcodec svq1 -qscale 3 -pix_fmt yuv410p"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_flashsv" ] ; then
do_video_encoding flashsv.flv "" "-an -vcodec flashsv -sws_flags neighbor+full_chroma_int+accurate_rnd+bitexact"
do_video_decoding "" "-pix_fmt yuv420p -sws_flags area+accurate_rnd+bitexact"
fi

if [ -n "$do_mp2" ] ; then
do_audio_encoding mp2.mp2 "-ar 44100"
do_audio_decoding
$tiny_psnr $pcm_dst $pcm_ref 2 1924 >> $logfile
fi

if [ -n "$do_ac3" ] ; then
do_audio_encoding ac3.rm "" -vn
# gcc 2.95.3 compiled binaries decode ac3 differently because of missing SSE support
#do_audio_decoding
#$tiny_psnr $pcm_dst $pcm_ref 2 1024 >> $logfile
fi

if [ -n "$do_g726" ] ; then
do_audio_encoding g726.wav "-ar 44100" "-ab 32k -ac 1 -ar 8000 -acodec g726"
do_audio_decoding
fi

if [ -n "$do_adpcm_ima_wav" ] ; then
do_audio_encoding adpcm_ima.wav "-ar 44100" "-acodec adpcm_ima_wav"
do_audio_decoding
fi

if [ -n "$do_adpcm_ima_qt" ] ; then
do_audio_encoding adpcm_qt.aiff "-ar 44100" "-acodec adpcm_ima_qt"
do_audio_decoding
fi

if [ -n "$do_adpcm_ms" ] ; then
do_audio_encoding adpcm_ms.wav "-ar 44100" "-acodec adpcm_ms"
do_audio_decoding
fi

if [ -n "$do_adpcm_yam" ] ; then
do_audio_encoding adpcm_yam.wav "-ar 44100" "-acodec adpcm_yamaha"
do_audio_decoding
fi

if [ -n "$do_adpcm_swf" ] ; then
do_audio_encoding adpcm_swf.flv "-ar 44100" "-acodec adpcm_swf"
do_audio_decoding
fi

if [ -n "$do_flac" ] ; then
do_audio_encoding flac.flac "-ar 44100" "-acodec flac -compression_level 2"
do_audio_decoding
fi

if [ -n "$do_wma" ] ; then
# wmav1
do_audio_encoding wmav1.asf "-ar 44100" "-acodec wmav1"
do_ffmpeg_nomd5 $pcm_dst -i $target_path/$file -f wav
$tiny_psnr $pcm_dst $pcm_ref 2 8192 >> $logfile
# wmav2
do_audio_encoding wmav2.asf "-ar 44100" "-acodec wmav2"
do_ffmpeg_nomd5 $pcm_dst -i $target_path/$file -f wav
$tiny_psnr $pcm_dst $pcm_ref 2 8192 >> $logfile
fi

#if [ -n "$do_vorbis" ] ; then
# vorbis
#disabled because it is broken
#do_audio_encoding vorbis.asf "-ar 44100" "-acodec vorbis"
#do_audio_decoding
#fi

do_audio_enc_dec() {
    do_audio_encoding $3.$1 "" "$4 -sample_fmt $2 -acodec $3"
    do_audio_decoding
}

if [ -n "$do_pcm" ] ; then
do_audio_enc_dec wav s16 pcm_alaw
do_audio_enc_dec wav s16 pcm_mulaw
do_audio_enc_dec mov u8 pcm_s8
do_audio_enc_dec wav u8 pcm_u8
do_audio_enc_dec mov s16 pcm_s16be
do_audio_enc_dec wav s16 pcm_s16le
do_audio_enc_dec mkv s16 pcm_s16be
do_audio_enc_dec mkv s16 pcm_s16le
do_audio_enc_dec mov s32 pcm_s24be
do_audio_enc_dec wav s32 pcm_s24le
#do_audio_enc_dec ??? s32 pcm_u24be #no compatible muxer or demuxer
#do_audio_enc_dec ??? s32 pcm_u24le #no compatible muxer or demuxer
do_audio_enc_dec mov s32 pcm_s32be
do_audio_enc_dec wav s32 pcm_s32le
#do_audio_enc_dec ??? s32 pcm_u32be #no compatible muxer or demuxer
#do_audio_enc_dec ??? s32 pcm_u32le #no compatible muxer or demuxer
do_audio_enc_dec au  flt pcm_f32be
do_audio_enc_dec wav flt pcm_f32le
do_audio_enc_dec au  dbl pcm_f64be
do_audio_enc_dec wav dbl pcm_f64le
do_audio_enc_dec wav s16 pcm_zork
do_audio_enc_dec 302 s16 pcm_s24daud "-ac 6 -ar 96000"
fi

# libavformat testing

if [ -n "$do_avi" ] ; then
do_libav avi
fi

if [ -n "$do_asf" ] ; then
do_libav asf "-acodec mp2" "-r 25"
fi

if [ -n "$do_rm" ] ; then
file=${outfile}libav.rm
do_ffmpeg $file -t 1 -qscale 10 -f image2 -vcodec pgmyuv -i $raw_src -f s16le -i $pcm_src
# broken
#do_ffmpeg_crc $file -i $target_path/$file
fi

if [ -n "$do_mpg" ] ; then
do_libav mpg
fi

if [ -n "$do_mxf" ] ; then
do_libav mxf "-ar 48000 -bf 2 -timecode_frame_start 264363"
do_libav mxf_d10 "-ar 48000 -ac 2 -r 25 -s 720x576 -padtop 32 -vcodec mpeg2video -intra -flags +ildct+low_delay -dc 10 -flags2 +ivlc+non_linear_q -qscale 1 -ps 1 -qmin 1 -rc_max_vbv_use 1 -rc_min_vbv_use 1 -pix_fmt yuv422p -minrate 30000k -maxrate 30000k -b 30000k -bufsize 1200000 -top 1 -rc_init_occupancy 1200000 -qmax 12 -f mxf_d10"
fi

if [ -n "$do_ts" ] ; then
do_libav ts
fi

if [ -n "$do_swf" ] ; then
do_libav swf -an
fi

if [ -n "$do_ffm" ] ; then
do_libav ffm
fi

if [ -n "$do_flv_fmt" ] ; then
do_libav flv -an
fi

if [ -n "$do_mov" ] ; then
do_libav mov "-acodec pcm_alaw"
fi

if [ -n "$do_dv_fmt" ] ; then
do_libav dv "-ar 48000 -r 25 -s pal -ac 2"
fi

if [ -n "$do_gxf" ] ; then
do_libav gxf "-ar 48000 -r 25 -s pal -ac 1"
fi

if [ -n "$do_nut" ] ; then
do_libav nut "-acodec mp2"
fi

if [ -n "$do_mkv" ] ; then
do_libav mkv
fi


# streamed images
# mjpeg
#file=${outfile}libav.mjpeg
#do_ffmpeg $file -t 1 -qscale 10 -f image2 -vcodec pgmyuv -i $raw_src
#do_ffmpeg_crc $file -i $target_path/$file

if [ -n "$do_pbmpipe" ] ; then
do_streamed_images pbm
fi

if [ -n "$do_pgmpipe" ] ; then
do_streamed_images pgm
fi

if [ -n "$do_ppmpipe" ] ; then
do_streamed_images ppm
fi

if [ -n "$do_gif" ] ; then
file=${outfile}libav.gif
do_ffmpeg $file -t 1 -qscale 10 -f image2 -vcodec pgmyuv -i $raw_src -pix_fmt rgb24
#do_ffmpeg_crc $file -i $target_path/$file
fi

if [ -n "$do_yuv4mpeg" ] ; then
file=${outfile}libav.y4m
do_ffmpeg $file -t 1 -qscale 10 -f image2 -vcodec pgmyuv -i $raw_src
#do_ffmpeg_crc $file -i $target_path/$file
fi

# image formats

if [ -n "$do_pgm" ] ; then
do_image_formats pgm
fi

if [ -n "$do_ppm" ] ; then
do_image_formats ppm
fi

if [ -n "$do_bmp" ] ; then
do_image_formats bmp
fi

if [ -n "$do_tga" ] ; then
do_image_formats tga
fi

if [ -n "$do_tiff" ] ; then
do_image_formats tiff "-pix_fmt rgb24"
fi

if [ -n "$do_sgi" ] ; then
do_image_formats sgi
fi

if [ -n "$do_jpg" ] ; then
do_image_formats jpg "-flags +bitexact -dct fastint -idct simple -pix_fmt yuvj420p" "-f image2"
fi

# audio only

if [ -n "$do_wav" ] ; then
do_audio_only wav
fi

if [ -n "$do_alaw" ] ; then
do_audio_only al
fi

if [ -n "$do_mulaw" ] ; then
do_audio_only ul
fi

if [ -n "$do_au" ] ; then
do_audio_only au
fi

if [ -n "$do_mmf" ] ; then
do_audio_only mmf
fi

if [ -n "$do_aiff" ] ; then
do_audio_only aif
fi

if [ -n "$do_voc" ] ; then
do_audio_only voc
fi

if [ -n "$do_ogg" ] ; then
do_audio_only ogg
fi

# pix_fmt conversions

if [ -n "$do_pixfmt" ] ; then
conversions="yuv420p yuv422p yuv444p yuyv422 yuv410p yuv411p yuvj420p \
             yuvj422p yuvj444p rgb24 bgr24 rgb32 rgb565 rgb555 gray monow \
             monob yuv440p yuvj440p"
for pix_fmt in $conversions ; do
    file=${outfile}libav-${pix_fmt}.yuv
    do_ffmpeg_nocheck $file -r 1 -t 1 -f image2 -vcodec pgmyuv -i $raw_src \
                            -f rawvideo -s 352x288 -pix_fmt $pix_fmt $target_path/$raw_dst
    do_ffmpeg $file -f rawvideo -s 352x288 -pix_fmt $pix_fmt -i $target_path/$raw_dst \
                    -f rawvideo -s 352x288 -pix_fmt yuv444p
done
fi

rm -f "$bench" "$bench2"
