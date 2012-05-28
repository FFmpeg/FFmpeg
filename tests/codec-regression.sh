#!/bin/sh
#
# automatic regression test for avconv
#
#
#set -x

set -e

. $(dirname $0)/regression-funcs.sh

eval do_$test=y

# generate reference for quality check
if [ -n "$do_vref" ]; then
do_avconv $raw_ref -f image2 -vcodec pgmyuv -i $raw_src -an -f rawvideo
fi
if [ -n "$do_aref" ]; then
do_avconv $pcm_ref -b 128k -ac 2 -ar 44100 -f s16le -i $pcm_src -f wav
do_avconv $pcm_ref_1ch -b 128k -i $pcm_src_1ch -f wav
fi

if [ -n "$do_cljr" ] ; then
do_video_encoding cljr.avi "-an -vcodec cljr"
do_video_decoding
fi

if [ -n "$do_mpeg" ] ; then
# mpeg1
do_video_encoding mpeg1.mpg "-qscale 10 -f mpeg1video"
do_video_decoding
fi

if [ -n "$do_mpeg2" ] ; then
# mpeg2
do_video_encoding mpeg2.mpg "-qscale 10 -vcodec mpeg2video -f mpeg1video"
do_video_decoding
fi

if [ -n "$do_mpeg2_ivlc_qprd" ]; then
# mpeg2 encoding intra vlc qprd
do_video_encoding mpeg2ivlc-qprd.mpg "-vb 500k -bf 2 -trellis 1 -flags +mv0 -mpv_flags +qp_rd -intra_vlc 1 -cmp 2 -subcmp 2 -mbd rd -vcodec mpeg2video -f mpeg2video"
do_video_decoding
fi

if [ -n "$do_mpeg2_422" ]; then
#mpeg2 4:2:2 encoding
do_video_encoding mpeg2_422.mpg "-vb 1000k -bf 2 -trellis 1 -flags +mv0+ildct+ilme -mpv_flags +qp_rd -intra_vlc 1 -mbd rd -vcodec mpeg2video -pix_fmt yuv422p -f mpeg2video"
do_video_decoding
fi

if [ -n "$do_mpeg2_idct_int" ]; then
# mpeg2
do_video_encoding mpeg2_idct_int.mpg "-qscale 10 -vcodec mpeg2video -idct int -dct int -f mpeg1video"
do_video_decoding "-idct int"
fi

if [ -n "$do_mpeg2_ilace" ]; then
# mpeg2 encoding interlaced
do_video_encoding mpeg2i.mpg "-qscale 10 -vcodec mpeg2video -f mpeg1video -flags +ildct+ilme"
do_video_decoding
fi

if [ -n "$do_mpeg2thread" ] ; then
# mpeg2 encoding interlaced
do_video_encoding mpeg2thread.mpg "-qscale 10 -vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -threads 2 -slices 2"
do_video_decoding
fi

if [ -n "$do_mpeg2thread_ilace" ]; then
# mpeg2 encoding interlaced using intra vlc
do_video_encoding mpeg2threadivlc.mpg "-qscale 10 -vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -intra_vlc 1 -threads 2 -slices 2"
do_video_decoding

# mpeg2 encoding interlaced
#file=${outfile}mpeg2reuse.mpg
#do_avconv $file $DEC_OPTS -me_threshold 256 -i ${target_path}/${outfile}mpeg2thread.mpg $ENC_OPTS -same_quant -me_threshold 256 -mb_threshold 1024 -vcodec mpeg2video -f mpeg1video -bf 2 -flags +ildct+ilme -threads 4
#do_video_decoding
fi

if [ -n "$do_msmpeg4v2" ] ; then
do_video_encoding msmpeg4v2.avi "-qscale 10 -an -vcodec msmpeg4v2"
do_video_decoding
fi

if [ -n "$do_msmpeg4" ] ; then
do_video_encoding msmpeg4.avi "-qscale 10 -an -vcodec msmpeg4"
do_video_decoding
fi

if [ -n "$do_msvideo1" ] ; then
do_video_encoding msvideo1.avi "-an -vcodec msvideo1"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_wmv1" ] ; then
do_video_encoding wmv1.avi "-qscale 10 -an -vcodec wmv1"
do_video_decoding
fi

if [ -n "$do_wmv2" ] ; then
do_video_encoding wmv2.avi "-qscale 10 -an -vcodec wmv2"
do_video_decoding
fi

if [ -n "$do_h261" ] ; then
do_video_encoding h261.avi "-qscale 11 -s 352x288 -an -vcodec h261"
do_video_decoding
fi

if [ -n "$do_h263" ] ; then
do_video_encoding h263.avi "-qscale 10 -s 352x288 -an -vcodec h263"
do_video_decoding
fi

if [ -n "$do_h263p" ] ; then
do_video_encoding h263p.avi "-qscale 2 -flags +aic -umv 1 -aiv 1 -s 352x288 -an -vcodec h263p -ps 300"
do_video_decoding
fi

if [ -n "$do_mpeg4" ] ; then
do_video_encoding odivx.mp4 "-flags +mv4 -mbd bits -qscale 10 -an -vcodec mpeg4"
do_video_decoding
fi

if [ -n "$do_huffyuv" ] ; then
do_video_encoding huffyuv.avi "-an -vcodec huffyuv -pix_fmt yuv422p -sws_flags neighbor+bitexact"
do_video_decoding "" "-strict -2 -pix_fmt yuv420p -sws_flags neighbor+bitexact"
fi

if [ -n "$do_amv" ] ; then
do_video_encoding amv.avi "-an -vcodec amv"
do_video_decoding
fi

if [ -n "$do_rc" ] ; then
do_video_encoding mpeg4-rc.avi "-b 400k -bf 2 -an -vcodec mpeg4"
do_video_decoding
fi

if [ -n "$do_mpeg4adv" ] ; then
do_video_encoding mpeg4-adv.avi "-qscale 9 -flags +mv4+aic -data_partitioning 1 -trellis 1 -mbd bits -ps 200 -an -vcodec mpeg4"
do_video_decoding
fi

if [ -n "$do_mpeg4_qprd" ]; then
do_video_encoding mpeg4-qprd.avi "-b 450k -bf 2 -trellis 1 -flags +mv4+mv0 -mpv_flags +qp_rd -cmp 2 -subcmp 2 -mbd rd -an -vcodec mpeg4"
do_video_decoding
fi

if [ -n "$do_mpeg4_adap" ]; then
do_video_encoding mpeg4-adap.avi "-b 550k -bf 2 -flags +mv4+mv0 -trellis 1 -cmp 1 -subcmp 2 -mbd rd -scplx_mask 0.3 -an -vcodec mpeg4"
do_video_decoding
fi

if [ -n "$do_mpeg4_qpel" ]; then
do_video_encoding mpeg4-Q.avi "-qscale 7 -flags +mv4+qpel -mbd 2 -bf 2 -cmp 1 -subcmp 2 -an -vcodec mpeg4"
do_video_decoding
fi

if [ -n "$do_mpeg4thread" ] ; then
do_video_encoding mpeg4-thread.avi "-b 500k -flags +mv4+aic -data_partitioning 1 -trellis 1 -mbd bits -ps 200 -bf 2 -an -vcodec mpeg4 -threads 2 -slices 2"
do_video_decoding
fi

if [ -n "$do_error" ] ; then
do_video_encoding error-mpeg4-adv.avi "-qscale 7 -flags +mv4+aic -data_partitioning 1 -mbd rd -ps 250 -error 10 -an -vcodec mpeg4"
do_video_decoding
fi

if [ -n "$do_mpeg4nr" ] ; then
do_video_encoding mpeg4-nr.avi "-qscale 8 -flags +mv4 -mbd rd -nr 200 -an -vcodec mpeg4"
do_video_decoding
fi

if [ -n "$do_mpeg1b" ] ; then
do_video_encoding mpeg1b.mpg "-qscale 8 -bf 3 -ps 200 -an -vcodec mpeg1video -f mpeg1video"
do_video_decoding
fi

if [ -n "$do_mjpeg" ] ; then
do_video_encoding mjpeg.avi "-qscale 9 -an -vcodec mjpeg -pix_fmt yuvj420p"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_jpeg2000" ] ; then
do_video_encoding jpeg2000.avi "-qscale 7 -an -vcodec j2k -strict experimental -pix_fmt rgb24"
do_video_decoding "-vcodec j2k -strict experimental" "-pix_fmt yuv420p"
fi

if [ -n "$do_ljpeg" ] ; then
do_video_encoding ljpeg.avi "-an -vcodec ljpeg -strict -1"
do_video_decoding
fi

if [ -n "$do_jpegls" ] ; then
do_video_encoding jpegls.avi "-an -vcodec jpegls -vtag MJPG -sws_flags neighbor+full_chroma_int+accurate_rnd+bitexact"
do_video_decoding "" "-pix_fmt yuv420p  -sws_flags area+bitexact"
fi

if [ -n "$do_rv10" ] ; then
do_video_encoding rv10.rm "-qscale 10 -an"
do_video_decoding
fi

if [ -n "$do_rv20" ] ; then
do_video_encoding rv20.rm "-qscale 10 -vcodec rv20 -an"
do_video_decoding
fi

if [ -n "$do_asv1" ] ; then
do_video_encoding asv1.avi "-qscale 10 -an -vcodec asv1"
do_video_decoding
fi

if [ -n "$do_asv2" ] ; then
do_video_encoding asv2.avi "-qscale 10 -an -vcodec asv2"
do_video_decoding
fi

if [ -n "$do_flv" ] ; then
do_video_encoding flv.flv "-qscale 10 -an -vcodec flv"
do_video_decoding
fi

if [ -n "$do_ffv1" ] ; then
do_video_encoding ffv1.avi "-strict -2 -an -vcodec ffv1"
do_video_decoding
fi

if [ -n "$do_ffvhuff" ] ; then
do_video_encoding ffvhuff.avi "-an -vcodec ffvhuff"
do_video_decoding ""
fi

if [ -n "$do_snow" ] ; then
do_video_encoding snow.avi "-strict -2 -an -vcodec snow -qscale 2 -flags +qpel -me_method iter -dia_size 2 -cmp 12 -subcmp 12 -s 128x64"
do_video_decoding "" "-s 352x288"
fi

if [ -n "$do_snowll" ] ; then
do_video_encoding snow53.avi "-strict -2 -an -vcodec snow -qscale .001 -pred 1 -flags +mv4+qpel"
do_video_decoding
fi

if [ -n "$do_dv" ] ; then
do_video_encoding dv.dv "-dct int -s pal -an"
do_video_decoding "" "-s cif"
fi

if [ -n "$do_dv_411" ]; then
do_video_encoding dv411.dv "-dct int -s pal -an -pix_fmt yuv411p -sws_flags area+accurate_rnd+bitexact"
do_video_decoding "" "-s cif -sws_flags area+accurate_rnd+bitexact"
fi

if [ -n "$do_dv50" ] ; then
do_video_encoding dv50.dv "-dct int -s pal -pix_fmt yuv422p -an -sws_flags neighbor+bitexact"
do_video_decoding "" "-s cif -pix_fmt yuv420p -sws_flags neighbor+bitexact"
fi

if [ -n "$do_dnxhd_1080i" ] ; then
# FIXME: interlaced raw DNxHD decoding is broken
do_video_encoding dnxhd-1080i.mov "-vcodec dnxhd -flags +ildct -s hd1080 -b 120M -pix_fmt yuv422p -vframes 5 -an"
do_video_decoding "" "-s cif -pix_fmt yuv420p"
fi

if [ -n "$do_dnxhd_720p" ] ; then
do_video_encoding dnxhd-720p.dnxhd "-s hd720 -b 90M -pix_fmt yuv422p -vframes 5 -an"
do_video_decoding "" "-s cif -pix_fmt yuv420p"
fi

if [ -n "$do_dnxhd_720p_rd" ] ; then
do_video_encoding dnxhd-720p-rd.dnxhd "-threads 4 -mbd rd -s hd720 -b 90M -pix_fmt yuv422p -vframes 5 -an"
do_video_decoding "" "-s cif -pix_fmt yuv420p"
fi

if [ -n "$do_dnxhd_720p_10bit" ] ; then
do_video_encoding dnxhd-720p-10bit.dnxhd "-s hd720 -b 90M -pix_fmt yuv422p10 -vframes 5 -an"
do_video_decoding "" "-s cif -pix_fmt yuv420p"
fi

if [ -n "$do_mpng" ] ; then
do_video_encoding mpng.avi "-an -vcodec png"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_prores" ] ; then
do_video_encoding prores.mov "-vcodec prores"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_prores_kostya" ] ; then
do_video_encoding prores_kostya.mov "-vcodec prores_kostya -profile hq"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_svq1" ] ; then
do_video_encoding svq1.mov "-an -vcodec svq1 -qscale 3 -pix_fmt yuv410p"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_flashsv" ] ; then
do_video_encoding flashsv.flv "-an -vcodec flashsv -sws_flags neighbor+full_chroma_int+accurate_rnd+bitexact"
do_video_decoding "" "-pix_fmt yuv420p -sws_flags area+accurate_rnd+bitexact"
fi

if [ -n "$do_flashsv2" ] ; then
do_video_encoding flashsv2.flv "-an -vcodec flashsv2 -sws_flags neighbor+full_chroma_int+accurate_rnd+bitexact -strict experimental -compression_level 0"
do_video_encoding flashsv2I.flv "-an -vcodec flashsv2 -sws_flags neighbor+full_chroma_int+accurate_rnd+bitexact -strict experimental -g 1"
do_video_decoding "" "-pix_fmt yuv420p -sws_flags area+accurate_rnd+bitexact"
fi

if [ -n "$do_roq" ] ; then
do_video_encoding roqav.roq "-vframes 5"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_qtrle" ] ; then
do_video_encoding qtrle.mov "-an -vcodec qtrle"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_qtrlegray" ] ; then
do_video_encoding qtrlegray.mov "-an -vcodec qtrle -pix_fmt gray"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_rgb" ] ; then
do_video_encoding rgb.avi "-an -vcodec rawvideo -pix_fmt bgr24"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_r210" ] ; then
do_video_encoding r210.avi "-an -c:v r210"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_v210" ] ; then
do_video_encoding v210.avi "-an -c:v v210"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_v308" ] ; then
do_video_encoding v308.avi "-an -c:v v308"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_v408" ] ; then
do_video_encoding v408.avi "-an -c:v v408 -sws_flags neighbor+bitexact"
do_video_decoding "" "-sws_flags neighbor+bitexact -pix_fmt yuv420p"
fi

if [ -n "$do_avui" ] ; then
do_video_encoding avui.mov "-s pal -an -c:v avui -strict experimental -sws_flags neighbor+bitexact"
do_video_decoding "" "-s cif -sws_flags neighbor+bitexact -pix_fmt yuv420p"
fi

if [ -n "$do_yuv" ] ; then
do_video_encoding yuv.avi "-an -vcodec rawvideo -pix_fmt yuv420p"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_yuv4" ] ; then
do_video_encoding yuv4.avi "-an -c:v yuv4"
do_video_decoding
fi

if [ -n "$do_y41p" ] ; then
do_video_encoding y41p.avi "-an -c:v y41p"
do_video_decoding
fi

if [ -n "$do_zlib" ] ; then
do_video_encoding zlib.avi "-an -vcodec zlib"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_zmbv" ] ; then
# default level of 9 leads to different results with
# different zlib versions, and even with 0 md5 differs
do_video_encoding_nomd5 zmbv.avi "-an -vcodec zmbv -compression_level 0"
do_video_decoding "" "-pix_fmt yuv420p"
fi

if [ -n "$do_mp2" ] ; then
do_audio_encoding mp2.mp2
do_audio_decoding
$tiny_psnr $pcm_dst $pcm_ref 2 1924
fi

if [ -n "$do_ac3_fixed" ] ; then
do_audio_encoding ac3.ac3 "-vn -acodec ac3_fixed"
# binaries configured with --disable-sse decode ac3 differently
#do_audio_decoding
#$tiny_psnr $pcm_dst $pcm_ref 2 1024
fi

if [ -n "$do_g723_1" ] ; then
do_audio_encoding g723_1.tco "-b:a 6.3k -ac 1 -ar 8000 -acodec g723_1"
do_audio_decoding
fi

if [ -n "$do_adpcm_adx" ] ; then
do_audio_encoding adpcm_adx.adx "-acodec adpcm_adx"
do_audio_decoding
fi

if [ -n "$do_adpcm_ima_wav" ] ; then
do_audio_encoding adpcm_ima.wav "-acodec adpcm_ima_wav"
do_audio_decoding
fi

if [ -n "$do_adpcm_ima_qt" ] ; then
do_audio_encoding adpcm_qt.aiff "-acodec adpcm_ima_qt"
do_audio_decoding
fi

if [ -n "$do_adpcm_ms" ] ; then
do_audio_encoding adpcm_ms.wav "-acodec adpcm_ms"
do_audio_decoding
fi

if [ -n "$do_adpcm_yam" ] ; then
do_audio_encoding adpcm_yam.wav "-acodec adpcm_yamaha"
do_audio_decoding
fi

if [ -n "$do_adpcm_swf" ] ; then
do_audio_encoding adpcm_swf.flv "-acodec adpcm_swf"
do_audio_decoding
fi

if [ -n "$do_alac" ] ; then
do_audio_encoding alac.m4a "-acodec alac -compression_level 1"
do_audio_decoding
fi

if [ -n "$do_flac" ] ; then
do_audio_encoding flac.flac "-acodec flac -compression_level 2"
do_audio_decoding
fi

if [ -n "$do_dca" ] ; then
do_audio_encoding dca.dts "-strict -2 -acodec dca"
# decoding is not bit-exact, so skip md5 of decoded file
do_audio_decoding_nomd5
$tiny_psnr $pcm_dst $pcm_ref 2 1920
fi

if [ -n "$do_ra144" ] ; then
do_audio_encoding ra144.ra "-ac 1 -acodec real_144"
do_audio_decoding "-ac 2"
$tiny_psnr $pcm_dst $pcm_ref 2 640
fi

if [ -n "$do_roqaudio" ] ; then
do_audio_encoding roqaudio.roq "-ar 22050 -acodec roq_dpcm"
do_audio_decoding "-ar 44100"
fi

# AAC and nellymoser are not bit-exact across platforms,
# they were moved to enc_dec_pcm tests instead.

#if [ -n "$do_vorbis" ] ; then
# vorbis
#disabled because it is broken
#do_audio_encoding vorbis.asf "-acodec vorbis"
#do_audio_decoding
#fi

do_audio_enc_dec() {
    do_audio_encoding $3.$1 "$4 -sample_fmt $2 -acodec $3"
    do_audio_decoding
}

if [ -n "$do_pcm_alaw" ] ; then
do_audio_enc_dec wav s16 pcm_alaw
fi
if [ -n "$do_pcm_mulaw" ] ; then
do_audio_enc_dec wav s16 pcm_mulaw
fi
if [ -n "$do_pcm_s8" ] ; then
do_audio_enc_dec mov u8 pcm_s8
fi
if [ -n "$do_pcm_u8" ] ; then
do_audio_enc_dec wav u8 pcm_u8
fi
if [ -n "$do_pcm_s16be" ] ; then
do_audio_enc_dec mov s16 pcm_s16be
fi
if [ -n "$do_pcm_s16le" ] ; then
do_audio_enc_dec wav s16 pcm_s16le
fi
if [ -n "$do_pcm_s24be" ] ; then
do_audio_enc_dec mov s32 pcm_s24be
fi
if [ -n "$do_pcm_s24le" ] ; then
do_audio_enc_dec wav s32 pcm_s24le
fi
# no compatible muxer or demuxer
# if [ -n "$do_pcm_u24be" ] ; then
# do_audio_enc_dec ??? u32 pcm_u24be
# fi
# if [ -n "$do_pcm_u24le" ] ; then
# do_audio_enc_dec ??? u32 pcm_u24le
# fi
if [ -n "$do_pcm_s32be" ] ; then
do_audio_enc_dec mov s32 pcm_s32be
fi
if [ -n "$do_pcm_s32le" ] ; then
do_audio_enc_dec wav s32 pcm_s32le
fi
# no compatible muxer or demuxer
# if [ -n "$do_pcm_u32be" ] ; then
# do_audio_enc_dec ??? u32 pcm_u32be
# fi
# if [ -n "$do_pcm_u32le" ] ; then
# do_audio_enc_dec ??? u32 pcm_u32le
# fi
if [ -n "$do_pcm_f32be" ] ; then
do_audio_enc_dec au  flt pcm_f32be
fi
if [ -n "$do_pcm_f32le" ] ; then
do_audio_enc_dec wav flt pcm_f32le
fi
if [ -n "$do_pcm_f64be" ] ; then
do_audio_enc_dec au  dbl pcm_f64be
fi
if [ -n "$do_pcm_f64le" ] ; then
do_audio_enc_dec wav dbl pcm_f64le
fi
