FATE_FLVENC_FFMPEG_FFPROBE-$(call TRANSCODE, FLV, FLV, RAWVIDEO_DECODER SCALE_FILTER TESTSRC_FILTER LAVFI_INDEV) += fate-flv-add_keyframe_index
fate-flv-add_keyframe_index: CMD = transcode "lavfi -graph testsrc=r=7:n=2:d=20" "foo" flv "-vf scale -c:v flv1 -dct int -g 7 -flvflags add_keyframe_index" "-c copy -t 0.1" "-show_entries format_tags"

FATE_ENHANCED_FLVENC_FFMPEG-$(call REMUX, FLV MOV, FLV_DEMUXER HEVC_PARSER) += fate-enhanced-flv-hevc
fate-enhanced-flv-hevc: CMD = transcode mov $(TARGET_SAMPLES)/hevc/dv84.mov\
		flv "-c copy" "-c copy"

FATE_FFMPEG_FFPROBE += $(FATE_FLVENC_FFMPEG_FFPROBE-yes)
FATE_SAMPLES_FFMPEG += $(FATE_ENHANCED_FLVENC_FFMPEG-yes)
fate-flvenc: $(FATE_FLVENC_FFMPEG_FFPROBE-yes) $(FATE_ENHANCED_FLVENC_FFMPEG-yes)
