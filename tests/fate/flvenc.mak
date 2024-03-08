FATE_FLVENC_FFMPEG_FFPROBE-$(call TRANSCODE, FLV, FLV, RAWVIDEO_DECODER SCALE_FILTER TESTSRC_FILTER LAVFI_INDEV) += fate-flv-add_keyframe_index
fate-flv-add_keyframe_index: CMD = transcode "lavfi -graph testsrc=r=7:n=2:d=20" "foo" flv "-vf scale -c:v flv1 -dct int -g 7 -flvflags add_keyframe_index" "-c copy -t 0.1" "-show_entries format_tags"

FATE_ENHANCED_FLVENC_FFMPEG-$(call REMUX, FLV MOV, FLV_DEMUXER HEVC_PARSER) += fate-enhanced-flv-hevc
fate-enhanced-flv-hevc: CMD = transcode mov $(TARGET_SAMPLES)/hevc/dv84.mov\
		flv "-c copy" "-c copy"

FATE_ENHANCED_FLVENC_FFMPEG-$(call REMUX, FLV IVF, FLV_DEMUXER VP9_PARSER) += fate-enhanced-flv-vp9
fate-enhanced-flv-vp9: CMD = transcode ivf $(TARGET_SAMPLES)/vp9-test-vectors/vp90-2-05-resize.ivf\
		flv "-c copy" "-c copy"

FATE_ENHANCED_FLVENC_FFMPEG-$(call REMUX, FLV IVF, FLV_DEMUXER AV1_DECODER AV1_PARSER) += fate-enhanced-flv-av1
fate-enhanced-flv-av1: CMD = stream_remux ivf $(TARGET_SAMPLES)/av1/seq_hdr_op_param_info.ivf "-c:v av1" \
		flv "-c copy" "-c:v av1" "-c copy"

FATE_FFMPEG_FFPROBE += $(FATE_FLVENC_FFMPEG_FFPROBE-yes)
FATE_SAMPLES_FFMPEG += $(FATE_ENHANCED_FLVENC_FFMPEG-yes)
fate-flvenc: $(FATE_FLVENC_FFMPEG_FFPROBE-yes) $(FATE_ENHANCED_FLVENC_FFMPEG-yes)
