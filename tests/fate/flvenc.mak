FATE_FLVENC_FFMPEG_FFPROBE-$(call TRANSCODE, FLV, FLV, RAWVIDEO_DECODER SCALE_FILTER TESTSRC_FILTER LAVFI_INDEV) += fate-flv-add_keyframe_index
fate-flv-add_keyframe_index: CMD = transcode "lavfi -graph testsrc=r=7:n=2:d=20" "foo" flv "-vf scale -c:v flv1 -dct int -g 7 -flvflags add_keyframe_index" "-c copy -t 0.1" "-show_entries format_tags"

FATE_FFMPEG_FFPROBE += $(FATE_FLVENC_FFMPEG_FFPROBE-yes)
fate-flvenc: $(FATE_FLVENC_FFMPEG_FFPROBE-yes)
