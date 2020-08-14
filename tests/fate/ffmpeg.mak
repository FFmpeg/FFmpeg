FATE_MAPCHAN-$(CONFIG_CHANNELMAP_FILTER) += fate-mapchan-6ch-extract-2
fate-mapchan-6ch-extract-2: tests/data/asynth-22050-6.wav
fate-mapchan-6ch-extract-2: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/asynth-22050-6.wav -map_channel 0.0.0 -fflags +bitexact -f wav md5: -map_channel 0.0.1 -fflags +bitexact -f wav md5:

FATE_MAPCHAN-$(CONFIG_CHANNELMAP_FILTER) += fate-mapchan-6ch-extract-2-downmix-mono
fate-mapchan-6ch-extract-2-downmix-mono: tests/data/asynth-22050-6.wav
fate-mapchan-6ch-extract-2-downmix-mono: CMD = md5 -auto_conversion_filters -i $(TARGET_PATH)/tests/data/asynth-22050-6.wav -map_channel 0.0.1 -map_channel 0.0.0 -ac 1 -fflags +bitexact -f wav

FATE_MAPCHAN-$(CONFIG_CHANNELMAP_FILTER) += fate-mapchan-silent-mono
fate-mapchan-silent-mono: tests/data/asynth-22050-1.wav
fate-mapchan-silent-mono: CMD = md5 -i $(TARGET_PATH)/tests/data/asynth-22050-1.wav -map_channel -1 -map_channel 0.0.0 -fflags +bitexact -f wav

FATE_MAPCHAN-$(CONFIG_CHANNELMAP_FILTER) += fate-mapchan-2ch-extract-ch0-ch2-trailing
fate-mapchan-2ch-extract-ch0-ch2-trailing: tests/data/asynth-44100-2.wav
fate-mapchan-2ch-extract-ch0-ch2-trailing: CMD = md5 -i $(TARGET_PATH)/tests/data/asynth-44100-2.wav -map_channel 0.0.0 -map_channel 0.0.2? -fflags +bitexact -f wav

FATE_MAPCHAN-$(CONFIG_CHANNELMAP_FILTER) += fate-mapchan-3ch-extract-ch0-ch2-trailing
fate-mapchan-3ch-extract-ch0-ch2-trailing: tests/data/asynth-44100-3.wav
fate-mapchan-3ch-extract-ch0-ch2-trailing: CMD = md5 -i $(TARGET_PATH)/tests/data/asynth-44100-3.wav -map_channel 0.0.0 -map_channel 0.0.2? -fflags +bitexact -f wav

FATE_MAPCHAN = $(FATE_MAPCHAN-yes)

FATE_FFMPEG += $(FATE_MAPCHAN)
fate-mapchan: $(FATE_MAPCHAN)

FATE_FFMPEG-$(CONFIG_COLOR_FILTER) += fate-ffmpeg-filter_complex
fate-ffmpeg-filter_complex: CMD = framecrc -filter_complex color=d=1:r=5 -fflags +bitexact

# Ticket 6603
FATE_FFMPEG-$(call ALLYES, AEVALSRC_FILTER ASETNSAMPLES_FILTER AC3_FIXED_ENCODER) += fate-ffmpeg-filter_complex_audio
fate-ffmpeg-filter_complex_audio: CMD = framecrc -auto_conversion_filters -filter_complex "aevalsrc=0:d=0.1,asetnsamples=1537" -c ac3_fixed

# Ticket 6375, use case of NoX
FATE_SAMPLES_FFMPEG-$(call ALLYES, MOV_DEMUXER PNG_DECODER ALAC_DECODER PCM_S16LE_ENCODER RAWVIDEO_ENCODER) += fate-ffmpeg-attached_pics
fate-ffmpeg-attached_pics: CMD = threads=2 framecrc -i $(TARGET_SAMPLES)/lossless-audio/inside.m4a -c:a pcm_s16le -max_muxing_queue_size 16 -af aresample

FATE_SAMPLES_FFMPEG-$(CONFIG_COLORKEY_FILTER) += fate-ffmpeg-filter_colorkey
fate-ffmpeg-filter_colorkey: tests/data/filtergraphs/colorkey
fate-ffmpeg-filter_colorkey: CMD = framecrc -auto_conversion_filters -idct simple -fflags +bitexact -flags +bitexact  -sws_flags +accurate_rnd+bitexact -i $(TARGET_SAMPLES)/cavs/cavs.mpg -fflags +bitexact -flags +bitexact -sws_flags +accurate_rnd+bitexact -i $(TARGET_SAMPLES)/lena.pnm -an -filter_complex_script $(TARGET_PATH)/tests/data/filtergraphs/colorkey -sws_flags +accurate_rnd+bitexact -fflags +bitexact -flags +bitexact -qscale 2 -frames:v 10

FATE_FFMPEG-$(CONFIG_COLOR_FILTER) += fate-ffmpeg-lavfi
fate-ffmpeg-lavfi: CMD = framecrc -lavfi color=d=1:r=5 -fflags +bitexact

FATE_SAMPLES_FFMPEG-$(CONFIG_RAWVIDEO_DEMUXER) += fate-force_key_frames
fate-force_key_frames: tests/data/vsynth_lena.yuv
fate-force_key_frames: CMD = enc_dec \
  "rawvideo -s 352x288 -pix_fmt yuv420p" tests/data/vsynth_lena.yuv \
  avi "-c mpeg4 -g 240 -qscale 10 -force_key_frames 0.5,0:00:01.5" \
  framecrc "" "" "-skip_frame nokey"

FATE_SAMPLES_FFMPEG-$(call ALLYES, VOBSUB_DEMUXER DVDSUB_DECODER AVFILTER OVERLAY_FILTER DVDSUB_ENCODER) += fate-sub2video
fate-sub2video: tests/data/vsynth_lena.yuv
fate-sub2video: CMD = framecrc -auto_conversion_filters \
  -f rawvideo -r 5 -s 352x288 -pix_fmt yuv420p -i $(TARGET_PATH)/tests/data/vsynth_lena.yuv \
  -ss 132 -i $(TARGET_SAMPLES)/sub/vobsub.idx \
  -filter_complex "sws_flags=+accurate_rnd+bitexact\;[0:0]scale=720:480[v]\;[v][1:0]overlay[v2]" \
  -map "[v2]" -c:v rawvideo -map 1:s -c:s dvdsub

# Very basic sub2video example, decode and convert to AVFrame with sub2video.
# Attempt to not touch timestamps.
FATE_SAMPLES_FFMPEG-$(call ALLYES, VOBSUB_DEMUXER DVDSUB_DECODER AVFILTER) += fate-sub2video_basic
fate-sub2video_basic: CMD = framecrc -auto_conversion_filters \
  -i $(TARGET_SAMPLES)/sub/vobsub.idx \
  -vsync passthrough -copyts \
  -filter_complex "sws_flags=+accurate_rnd+bitexact\;[0:s:0]scale" \
  -c:v rawvideo

# Time-limited run with a sample that doesn't require seeking and
# contains samples within the initial period.
FATE_SAMPLES_FFMPEG-$(call ALLYES, SUP_DEMUXER PGSSUB_DECODER AVFILTER) += fate-sub2video_time_limited
fate-sub2video_time_limited: CMD = framecrc -auto_conversion_filters \
  -i $(TARGET_SAMPLES)/sub/pgs_sub.sup \
  -vsync passthrough -copyts \
  -t 15 \
  -filter_complex "sws_flags=+accurate_rnd+bitexact\;[0:s:0]scale" \
  -c:v rawvideo

FATE_FFMPEG-$(call ALLYES, PCM_S16LE_DEMUXER PCM_S16LE_MUXER PCM_S16LE_DECODER PCM_S16LE_ENCODER) += fate-unknown_layout-pcm
fate-unknown_layout-pcm: $(AREF)
fate-unknown_layout-pcm: CMD = md5 \
  -guess_layout_max 0 -f s16le -ac 1 -ar 44100 -i $(TARGET_PATH)/$(AREF) -f s16le

FATE_FFMPEG-$(call ALLYES, PCM_S16LE_DEMUXER AC3_MUXER PCM_S16LE_DECODER AC3_FIXED_ENCODER) += fate-unknown_layout-ac3
fate-unknown_layout-ac3: $(AREF)
fate-unknown_layout-ac3: CMD = md5 -auto_conversion_filters \
  -guess_layout_max 0 -f s16le -ac 1 -ar 44100 -i $(TARGET_PATH)/$(AREF) \
  -f ac3 -flags +bitexact -c ac3_fixed


FATE_STREAMCOPY-$(call ALLYES, EAC3_DEMUXER MOV_MUXER) += fate-copy-trac3074
fate-copy-trac3074: $(SAMPLES)/eac3/csi_miami_stereo_128_spx.eac3
fate-copy-trac3074: CMD = transcode eac3 $(TARGET_SAMPLES)/eac3/csi_miami_stereo_128_spx.eac3\
                     mp4 "-codec copy -map 0" "-codec copy"

FATE_STREAMCOPY-$(call ALLYES, MOV_DEMUXER MOV_MUXER) += fate-copy-trac236
fate-copy-trac236: $(SAMPLES)/mov/fcp_export8-236.mov
fate-copy-trac236: CMD = transcode mov $(TARGET_SAMPLES)/mov/fcp_export8-236.mov\
                     mov "-codec copy -map 0"

FATE_STREAMCOPY-$(call ALLYES, MPEGTS_DEMUXER MXF_MUXER PCM_S16LE_ENCODER) += fate-copy-trac4914
fate-copy-trac4914: $(SAMPLES)/mpeg2/xdcam8mp2-1s_small.ts
fate-copy-trac4914: CMD = transcode mpegts $(TARGET_SAMPLES)/mpeg2/xdcam8mp2-1s_small.ts\
                      mxf "-c:a pcm_s16le -af aresample -c:v copy"

FATE_STREAMCOPY-$(call ALLYES, MPEGTS_DEMUXER AVI_MUXER) += fate-copy-trac4914-avi
fate-copy-trac4914-avi: $(SAMPLES)/mpeg2/xdcam8mp2-1s_small.ts
fate-copy-trac4914-avi: CMD = transcode mpegts $(TARGET_SAMPLES)/mpeg2/xdcam8mp2-1s_small.ts\
                          avi "-c:a copy -c:v copy" "-af aresample"

FATE_STREAMCOPY-$(call ALLYES, H264_DEMUXER AVI_MUXER) += fate-copy-trac2211-avi
fate-copy-trac2211-avi: $(SAMPLES)/h264/bbc2.sample.h264
fate-copy-trac2211-avi: CMD = transcode "h264 -r 14" $(TARGET_SAMPLES)/h264/bbc2.sample.h264\
                          avi "-c:a copy -c:v copy"

FATE_STREAMCOPY-$(call ENCDEC, APNG, APNG) += fate-copy-apng
fate-copy-apng: fate-lavf-apng
fate-copy-apng: CMD = transcode apng tests/data/lavf/lavf.apng apng "-c:v copy"

FATE_STREAMCOPY-$(call DEMMUX, OGG, OGG) += fate-limited_input_seek fate-limited_input_seek-copyts
fate-limited_input_seek: $(SAMPLES)/vorbis/moog_small.ogg
fate-limited_input_seek: CMD = md5 -ss 1.5 -t 1.3 -i $(TARGET_SAMPLES)/vorbis/moog_small.ogg -c:a copy -fflags +bitexact -f ogg
fate-limited_input_seek-copyts: $(SAMPLES)/vorbis/moog_small.ogg
fate-limited_input_seek-copyts: CMD = md5 -ss 1.5 -t 1.3 -i $(TARGET_SAMPLES)/vorbis/moog_small.ogg -c:a copy -copyts -fflags +bitexact -f ogg

FATE_STREAMCOPY-$(call ALLYES, MOV_DEMUXER MOV_MUXER) += fate-copy-psp
fate-copy-psp: $(SAMPLES)/h264/wwwq_cut.mp4
fate-copy-psp: CMD = transcode "mov" $(TARGET_SAMPLES)/h264/wwwq_cut.mp4\
                      psp "-c copy" "-codec copy"

FATE_STREAMCOPY-$(CONFIG_FLV_DEMUXER) += fate-ffmpeg-streamloop
fate-ffmpeg-streamloop: $(SAMPLES)/flv/streamloop.flv
fate-ffmpeg-streamloop: CMD = framemd5 -stream_loop 2 -i $(TARGET_SAMPLES)/flv/streamloop.flv -c copy

fate-streamcopy: $(FATE_STREAMCOPY-yes)

FATE_SAMPLES_FFMPEG-$(call ALLYES, MOV_DEMUXER MATROSKA_MUXER) += fate-rgb24-mkv
fate-rgb24-mkv: $(SAMPLES)/qtrle/aletrek-rle.mov
fate-rgb24-mkv: CMD = transcode "mov" $(TARGET_SAMPLES)/qtrle/aletrek-rle.mov\
                      matroska "-c:v rawvideo -pix_fmt rgb24 -allow_raw_vfw 1 -frames:v 1"

FATE_SAMPLES_FFMPEG-$(call ALLYES, AAC_DEMUXER MOV_MUXER) += fate-adtstoasc_ticket3715
fate-adtstoasc_ticket3715: $(SAMPLES)/aac/foo.aac
fate-adtstoasc_ticket3715: CMD = transcode "aac" $(TARGET_SAMPLES)/aac/foo.aac\
                      mov "-c copy -bsf:a aac_adtstoasc" "-codec copy"

FATE_SAMPLES_FFMPEG-$(call ALLYES, MOV_DEMUXER H264_MUXER H264_MP4TOANNEXB_BSF) += fate-h264_mp4toannexb_ticket2991
fate-h264_mp4toannexb_ticket2991: $(SAMPLES)/h264/wwwq_cut.mp4
fate-h264_mp4toannexb_ticket2991: CMD = transcode "mp4" $(TARGET_SAMPLES)/h264/wwwq_cut.mp4\
                                  h264 "-c:v copy -bsf:v h264_mp4toannexb" "-codec copy"

FATE_SAMPLES_FFMPEG-$(call ALLYES, MOV_DEMUXER H264_MUXER H264_MP4TOANNEXB_BSF) += fate-h264_mp4toannexb_ticket5927 fate-h264_mp4toannexb_ticket5927_2
fate-h264_mp4toannexb_ticket5927:   CMD = transcode "mp4" $(TARGET_SAMPLES)/h264/thezerotheorem-cut.mp4 \
                                          h264 "-c:v copy -bsf:v h264_mp4toannexb -an" "-c:v copy"
fate-h264_mp4toannexb_ticket5927_2: CMD = transcode "mp4" $(TARGET_SAMPLES)/h264/thezerotheorem-cut.mp4 \
                                          h264 "-c:v copy -an" "-c:v copy"

FATE_SAMPLES_FFMPEG-$(call ALLYES, MPEGPS_DEMUXER AVI_MUXER REMOVE_EXTRADATA_BSF) += fate-ffmpeg-bsf-remove-k fate-ffmpeg-bsf-remove-r fate-ffmpeg-bsf-remove-e
fate-ffmpeg-bsf-remove-k: $(SAMPLES)/mpeg2/matrixbench_mpeg2.lq1.mpg
fate-ffmpeg-bsf-remove-k: CMD = transcode "mpeg" $(TARGET_SAMPLES)/mpeg2/matrixbench_mpeg2.lq1.mpg\
                          avi "-vbsf remove_extra=k" "-codec copy"
fate-ffmpeg-bsf-remove-r: $(SAMPLES)/mpeg2/matrixbench_mpeg2.lq1.mpg
fate-ffmpeg-bsf-remove-r: CMD = transcode "mpeg" $(TARGET_SAMPLES)/mpeg2/matrixbench_mpeg2.lq1.mpg\
                          avi "-vbsf remove_extra=keyframe" "-codec copy"
fate-ffmpeg-bsf-remove-e: $(SAMPLES)/mpeg2/matrixbench_mpeg2.lq1.mpg
fate-ffmpeg-bsf-remove-e: CMD = transcode "mpeg" $(TARGET_SAMPLES)/mpeg2/matrixbench_mpeg2.lq1.mpg\
                          avi "-vbsf remove_extra=e" "-codec copy"


FATE_SAMPLES_FFMPEG-yes += $(FATE_STREAMCOPY-yes)

FATE_TIME_BASE-$(call ALLYES, MPEGPS_DEMUXER MXF_MUXER) += fate-time_base
fate-time_base: $(SAMPLES)/mpeg2/dvd_single_frame.vob
fate-time_base: CMD = md5 -i $(TARGET_SAMPLES)/mpeg2/dvd_single_frame.vob -an -sn -c:v copy -r 25 -time_base 1001:30000 -fflags +bitexact -f mxf

FATE_SAMPLES_FFMPEG-yes += $(FATE_TIME_BASE-yes)
