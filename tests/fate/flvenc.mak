tests/data/add_keyframe_index.flv: TAG = GEN
tests/data/add_keyframe_index.flv: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< \
		-f lavfi -i "sws_flags=+accurate_rnd+bitexact;testsrc=r=7:n=2:d=20" -sws_flags '+accurate_rnd+bitexact' -metadata "encoder=Lavf" -pix_fmt yuv420p -c:v flv1 -g 7 -f flv -flags +bitexact -fflags +bitexact \
		-flvflags add_keyframe_index -idct simple -dct int -y $(TARGET_PATH)/tests/data/add_keyframe_index.flv 2> /dev/null;

FATE_AFILTER-$(call ALLYES, FLV_MUXER FLV_DEMUXER AVDEVICE TESTSRC_FILTER LAVFI_INDEV FLV_ENCODER) += fate-flv-add_keyframe_index
fate-flv-add_keyframe_index: tests/data/add_keyframe_index.flv
fate-flv-add_keyframe_index: CMD = ffmetadata -flags +bitexact -i $(TARGET_PATH)/tests/data/add_keyframe_index.flv


