FATE_OPTIONS += fate-options-force_key_frames
fate-options-force_key_frames: tests/data/vsynth2.yuv
fate-options-force_key_frames: CMD = enc_dec \
  "rawvideo -s 352x288 -pix_fmt yuv420p" tests/data/vsynth2.yuv \
  avi "-c mpeg4 -g 240 -qscale 10 -force_key_frames 0.5,0:00:01.5" \
  framecrc "" "" "-skip_frame nokey"

FATE_FFMPEG += $(FATE_OPTIONS)
fate-options: $(FATE_OPTIONS)
