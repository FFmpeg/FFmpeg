tests/data/live_no_endlist.m3u8: TAG = GEN
tests/data/live_no_endlist.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -v verbose -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls -hls_time 3 -map 0 \
        -hls_flags omit_endlist -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/live_no_endlist_%03d.ts \
        $(TARGET_PATH)/tests/data/live_no_endlist.m3u8 2>/dev/null

FATE_HLSENC-$(call ALLYES, HLS_DEMUXER MPEGTS_MUXER MPEGTS_DEMUXER AEVALSRC_FILTER LAVFI_INDEV MP2FIXED_ENCODER) += fate-hls-live-no-endlist
fate-hls-live-no-endlist: tests/data/live_no_endlist.m3u8
fate-hls-live-no-endlist: SRC = $(TARGET_PATH)/tests/data/live_no_endlist.m3u8
fate-hls-live-no-endlist: CMD = md5 -i $(SRC) -af hdcd=process_stereo=false -t 6 -f s24le
fate-hls-live-no-endlist: CMP = oneline
fate-hls-live-no-endlist: REF = e038bb8e65d4c1745b9b3ed643e607a3

tests/data/live_last_endlist.m3u8: TAG = GEN
tests/data/live_last_endlist.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -v verbose -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls -hls_time 3 -map 0 \
        -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/live_last_endlist_%03d.ts \
        $(TARGET_PATH)/tests/data/live_last_endlist.m3u8 2>/dev/null

FATE_HLSENC-$(call ALLYES, HLS_DEMUXER MPEGTS_MUXER MPEGTS_DEMUXER AEVALSRC_FILTER LAVFI_INDEV MP2FIXED_ENCODER) += fate-hls-live-last-endlist
fate-hls-live-last-endlist: tests/data/live_last_endlist.m3u8
fate-hls-live-last-endlist: SRC = $(TARGET_PATH)/tests/data/live_last_endlist.m3u8
fate-hls-live-last-endlist: CMD = md5 -i $(SRC) -af hdcd=process_stereo=false -t 6 -f s24le
fate-hls-live-last-endlist: CMP = oneline
fate-hls-live-last-endlist: REF = 2ca8567092dcf01e37bedd50454d1ab7


tests/data/live_endlist.m3u8: TAG = GEN
tests/data/live_endlist.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls -hls_time 3 -map 0 \
        -hls_list_size 0 -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/live_endlist_%d.ts \
        $(TARGET_PATH)/tests/data/live_endlist.m3u8 2>/dev/null

FATE_HLSENC-$(call ALLYES, HLS_DEMUXER MPEGTS_MUXER MPEGTS_DEMUXER AEVALSRC_FILTER LAVFI_INDEV MP2FIXED_ENCODER) += fate-hls-live-endlist
fate-hls-live-endlist: tests/data/live_endlist.m3u8
fate-hls-live-endlist: SRC = $(TARGET_PATH)/tests/data/live_endlist.m3u8
fate-hls-live-endlist: CMD = md5 -i $(SRC) -af hdcd=process_stereo=false -t 20 -f s24le
fate-hls-live-endlist: CMP = oneline
fate-hls-live-endlist: REF = e189ce781d9c87882f58e3929455167b

tests/data/hls_segment_size.m3u8: TAG = GEN
tests/data/hls_segment_size.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls -hls_segment_size 300000 -map 0 \
	-hls_list_size 0 -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/hls_segment_size_%d.ts \
	$(TARGET_PATH)/tests/data/hls_segment_size.m3u8 2>/dev/null

FATE_HLSENC-$(call ALLYES, HLS_DEMUXER MPEGTS_MUXER MPEGTS_DEMUXER AEVALSRC_FILTER LAVFI_INDEV MP2FIXED_ENCODER) += fate-hls-segment-size
fate-hls-segment-size: tests/data/hls_segment_size.m3u8
fate-hls-segment-size: CMD = framecrc -auto_conversion_filters -flags +bitexact -i $(TARGET_PATH)/tests/data/hls_segment_size.m3u8 -vf setpts=N*23

tests/data/hls_segment_single.m3u8: TAG = GEN
tests/data/hls_segment_single.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls -hls_flags single_file -map 0 \
	-hls_list_size 0 -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/hls_segment_single.ts \
	$(TARGET_PATH)/tests/data/hls_segment_single.m3u8 2>/dev/null

FATE_HLSENC-$(call ALLYES, HLS_DEMUXER MPEGTS_MUXER MPEGTS_DEMUXER AEVALSRC_FILTER LAVFI_INDEV MP2FIXED_ENCODER) += fate-hls-segment-single
fate-hls-segment-single: tests/data/hls_segment_single.m3u8
fate-hls-segment-single: CMD = framecrc -auto_conversion_filters -flags +bitexact -i $(TARGET_PATH)/tests/data/hls_segment_single.m3u8 -vf setpts=N*23

tests/data/hls_init_time.m3u8: TAG = GEN
tests/data/hls_init_time.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=11" -f hls -hls_init_time 1 -hls_time 3 -map 0 \
	-hls_list_size 5 -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/hls_init_time_%d.ts \
	$(TARGET_PATH)/tests/data/hls_init_time.m3u8 2>/dev/null

FATE_HLSENC-$(call ALLYES, HLS_DEMUXER MPEGTS_MUXER MPEGTS_DEMUXER AEVALSRC_FILTER LAVFI_INDEV MP2FIXED_ENCODER) += fate-hls-init-time
fate-hls-init-time: tests/data/hls_init_time.m3u8
fate-hls-init-time: CMD = framecrc -auto_conversion_filters -flags +bitexact -i $(TARGET_PATH)/tests/data/hls_init_time.m3u8 -vf setpts=N*23

tests/data/hls_list_size.m3u8: TAG = GEN
tests/data/hls_list_size.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=20" -f hls -hls_time 4 -map 0 \
	-hls_list_size 4 -codec:a mp2fixed -hls_segment_filename $(TARGET_PATH)/tests/data/hls_list_size_%d.ts \
	$(TARGET_PATH)/tests/data/hls_list_size.m3u8 2>/dev/null

FATE_HLSENC-$(call ALLYES, HLS_DEMUXER MPEGTS_MUXER MPEGTS_DEMUXER AEVALSRC_FILTER LAVFI_INDEV MP2FIXED_ENCODER) += fate-hls-list-size
fate-hls-list-size: tests/data/hls_list_size.m3u8
fate-hls-list-size: CMD = framecrc -auto_conversion_filters -flags +bitexact -i $(TARGET_PATH)/tests/data/hls_list_size.m3u8 -vf setpts=N*23

tests/data/hls_fmp4.m3u8: TAG = GEN
tests/data/hls_fmp4.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -re -i "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=5" -map 0 -codec:a mp2fixed \
	-hls_segment_type mpegts -hls_fmp4_init_filename now.mp4 -hls_list_size 0 \
	-hls_time 1 -hls_segment_filename "$(TARGET_PATH)/tests/data/hls_fmp4_%d.m4s" \
	$(TARGET_PATH)/tests/data/hls_fmp4.m3u8 2>/dev/null

FATE_HLSENC-$(call ALLYES, HLS_DEMUXER MPEGTS_MUXER MPEGTS_DEMUXER AEVALSRC_FILTER LAVFI_INDEV MP2FIXED_ENCODER) += fate-hls-fmp4
fate-hls-fmp4: tests/data/hls_fmp4.m3u8
fate-hls-fmp4: CMD = framecrc -auto_conversion_filters -flags +bitexact -i $(TARGET_PATH)/tests/data/hls_fmp4.m3u8 -vf setpts=N*23

tests/data/hls_fmp4_ac3.m3u8: TAG = GEN
tests/data/hls_fmp4_ac3.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-stream_loop 4 -i $(SAMPLES)/ac3/monsters_inc_5.1_448_small.ac3 -c copy -map 0 \
	-hls_segment_type fmp4 -hls_fmp4_init_filename now_ac3.mp4 -hls_list_size 0 \
	-hls_time 2 -hls_segment_filename "$(TARGET_PATH)/tests/data/hls_fmp4_ac3_%d.m4s" \
	$(TARGET_PATH)/tests/data/hls_fmp4_ac3.m3u8 2>/dev/null

FATE_HLSENC_PROBE-$(call ALLYES, HLS_DEMUXER EAC3_DEMUXER) += fate-hls-fmp4_ac3
fate-hls-fmp4_ac3: tests/data/hls_fmp4_ac3.m3u8
fate-hls-fmp4_ac3: CMD = probeaudiostream $(TARGET_PATH)/tests/data/now_ac3.mp4

FATE_SAMPLES_FFMPEG += $(FATE_HLSENC-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_HLSENC_PROBE-yes)
fate-hlsenc: $(FATE_HLSENC-yes) $(FATE_HLSENC_PROBE-yes)
