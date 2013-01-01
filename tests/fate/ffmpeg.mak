FATE_MAPCHAN += fate-mapchan-6ch-extract-2
fate-mapchan-6ch-extract-2: tests/data/asynth-22050-6.wav
fate-mapchan-6ch-extract-2: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/asynth-22050-6.wav -map_channel 0.0.0 -flags +bitexact -f wav md5: -map_channel 0.0.1 -flags +bitexact -f wav md5:

FATE_MAPCHAN += fate-mapchan-6ch-extract-2-downmix-mono
fate-mapchan-6ch-extract-2-downmix-mono: tests/data/asynth-22050-6.wav
fate-mapchan-6ch-extract-2-downmix-mono: CMD = md5 -i $(TARGET_PATH)/tests/data/asynth-22050-6.wav -map_channel 0.0.1 -map_channel 0.0.0 -ac 1 -flags +bitexact -f wav

FATE_MAPCHAN += fate-mapchan-silent-mono
fate-mapchan-silent-mono: tests/data/asynth-22050-1.wav
fate-mapchan-silent-mono: CMD = md5 -i $(TARGET_PATH)/tests/data/asynth-22050-1.wav -map_channel -1 -map_channel 0.0.0 -flags +bitexact -f wav

FATE_FFMPEG += $(FATE_MAPCHAN)
fate-mapchan: $(FATE_MAPCHAN)

FATE_FFMPEG += fate-force_key_frames
fate-force_key_frames: tests/data/vsynth2.yuv
fate-force_key_frames: CMD = enc_dec \
  "rawvideo -s 352x288 -pix_fmt yuv420p" tests/data/vsynth2.yuv \
  avi "-c mpeg4 -g 240 -qscale 10 -force_key_frames 0.5,0:00:01.5" \
  framecrc "" "" "-skip_frame nokey"
