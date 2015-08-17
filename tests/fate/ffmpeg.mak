FATE_MAPCHAN-$(CONFIG_CHANNELMAP_FILTER) += fate-mapchan-6ch-extract-2
fate-mapchan-6ch-extract-2: tests/data/asynth-22050-6.wav
fate-mapchan-6ch-extract-2: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/asynth-22050-6.wav -map_channel 0.0.0 -fflags +bitexact -f wav md5: -map_channel 0.0.1 -fflags +bitexact -f wav md5:

FATE_MAPCHAN-$(CONFIG_CHANNELMAP_FILTER) += fate-mapchan-6ch-extract-2-downmix-mono
fate-mapchan-6ch-extract-2-downmix-mono: tests/data/asynth-22050-6.wav
fate-mapchan-6ch-extract-2-downmix-mono: CMD = md5 -i $(TARGET_PATH)/tests/data/asynth-22050-6.wav -map_channel 0.0.1 -map_channel 0.0.0 -ac 1 -fflags +bitexact -f wav

FATE_MAPCHAN-$(CONFIG_CHANNELMAP_FILTER) += fate-mapchan-silent-mono
fate-mapchan-silent-mono: tests/data/asynth-22050-1.wav
fate-mapchan-silent-mono: CMD = md5 -i $(TARGET_PATH)/tests/data/asynth-22050-1.wav -map_channel -1 -map_channel 0.0.0 -fflags +bitexact -f wav

FATE_MAPCHAN = $(FATE_MAPCHAN-yes)

FATE_FFMPEG += $(FATE_MAPCHAN)
fate-mapchan: $(FATE_MAPCHAN)

FATE_FFMPEG-$(CONFIG_COLOR_FILTER) += fate-ffmpeg-filter_complex
fate-ffmpeg-filter_complex: CMD = framecrc -filter_complex color=d=1:r=5 -fflags +bitexact

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
fate-sub2video: CMD = framecrc \
  -f rawvideo -r 5 -s 352x288 -pix_fmt yuv420p -i $(TARGET_PATH)/tests/data/vsynth_lena.yuv \
  -ss 132 -i $(TARGET_SAMPLES)/sub/vobsub.idx \
  -filter_complex "sws_flags=+accurate_rnd+bitexact\;[0:0]scale=720:480[v]\;[v][1:0]overlay[v2]" \
  -map "[v2]" -c:v rawvideo -map 1:s -c:s dvdsub

FATE_FFMPEG-$(call ALLYES, PCM_S16LE_DEMUXER PCM_S16LE_MUXER PCM_S16LE_DECODER PCM_S16LE_ENCODER) += fate-unknown_layout-pcm
fate-unknown_layout-pcm: $(AREF)
fate-unknown_layout-pcm: CMD = md5 \
  -guess_layout_max 0 -f s16le -ac 1 -ar 44100 -i $(TARGET_PATH)/$(AREF) -f s16le

FATE_FFMPEG-$(call ALLYES, PCM_S16LE_DEMUXER AC3_MUXER PCM_S16LE_DECODER AC3_FIXED_ENCODER) += fate-unknown_layout-ac3
fate-unknown_layout-ac3: $(AREF)
fate-unknown_layout-ac3: CMD = md5 \
  -guess_layout_max 0 -f s16le -ac 1 -ar 44100 -i $(TARGET_PATH)/$(AREF) \
  -f ac3 -flags +bitexact -c ac3_fixed
