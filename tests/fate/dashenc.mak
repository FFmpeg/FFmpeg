tests/data/dash_mpd_timing.mpd: TAG = GEN
tests/data/dash_mpd_timing.mpd: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(RM) $(TARGET_PATH)/tests/data/dash_mpd_timing*
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin -loglevel error -re \
	-f lavfi -i "testsrc2=size=128x72:rate=1" -map 0:v \
	-c:v mpeg4 -g 1 -streaming 1 -window_size 5 \
	-availability_start_time_ms 1700000000123 -suggested_presentation_delay 2s \
	-f dash $(TARGET_PATH)/tests/data/dash_mpd_timing.mpd & \
	pid=$$!; \
	i=0; \
	while ! test -s $(TARGET_PATH)/tests/data/dash_mpd_timing.mpd && test $$i -lt 100; do \
	    sleep 0.1; \
	    i=$$((i + 1)); \
	done; \
	test -s $(TARGET_PATH)/tests/data/dash_mpd_timing.mpd; \
	cp $(TARGET_PATH)/tests/data/dash_mpd_timing.mpd $(TARGET_PATH)/tests/data/dash_mpd_timing.live.mpd; \
	kill $$pid 2>/dev/null || true; \
	wait $$pid 2>/dev/null || true; \
	cp $(TARGET_PATH)/tests/data/dash_mpd_timing.live.mpd $(TARGET_PATH)/tests/data/dash_mpd_timing.mpd; \
	test -s $(TARGET_PATH)/tests/data/dash_mpd_timing.mpd

FATE_DASHENC_LAVFI-$(call ALLYES, TESTSRC2_FILTER LAVFI_INDEV MPEG4_ENCODER DASH_MUXER MP4_MUXER FILE_PROTOCOL) += fate-dash-mpd-timing
fate-dash-mpd-timing: tests/data/dash_mpd_timing.mpd
fate-dash-mpd-timing: CMD = sed -n -e /suggestedPresentationDelay=/p -e /availabilityStartTime=/p $(TARGET_PATH)/tests/data/dash_mpd_timing.mpd
fate-dash-mpd-timing: CMP = diff

tests/data/dash_big_init.mpd: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-filter_complex "aevalsrc=cos(2*PI*t)*sin(2*PI*(440+4*t)*t):d=2:s=16000,asplit=100" \
	-codec:a mp2fixed -b:a 8k -bitexact -f hls -hls_time 1 -hls_list_size 0 \
	-hls_segment_type fmp4 -hls_fmp4_init_filename dash_big_init.mp4 \
	-hls_segment_filename $(TARGET_PATH)/tests/data/dash_big_init_%d.m4s \
	$(TARGET_PATH)/tests/data/dash_big_init.m3u8 2>/dev/null
	$(Q)printf '%s\n' \
	'<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" profiles="urn:mpeg:dash:profile:full:2011" type="static" mediaPresentationDuration="PT2S" minBufferTime="PT1S">' \
	'<Period><AdaptationSet mimeType="audio/mp4"><Representation id="0" bandwidth="1000000">' \
	'<SegmentList timescale="16000" duration="16128"><Initialization sourceURL="dash_big_init.mp4"/>' \
	'<SegmentURL media="dash_big_init_0.m4s"/><SegmentURL media="dash_big_init_1.m4s"/>' \
	'</SegmentList></Representation></AdaptationSet></Period></MPD>' > $@

FATE_DASHENC_LAVFI-$(call ALLYES, AEVALSRC_FILTER ASPLIT_FILTER ARESAMPLE_FILTER MP2FIXED_ENCODER HLS_MUXER MP4_MUXER DASH_DEMUXER MOV_DEMUXER MP3_DECODER FILE_PROTOCOL FRAMECRC_MUXER PIPE_PROTOCOL) += fate-dash-big-init
fate-dash-big-init: tests/data/dash_big_init.mpd
fate-dash-big-init: CLEANFILES = tests/data/dash_big_init.mpd tests/data/dash_big_init.m3u8 tests/data/dash_big_init.mp4 tests/data/dash_big_init_*.m4s
fate-dash-big-init: CMD = framecrc -i $(TARGET_PATH)/tests/data/dash_big_init.mpd -map 0:a:99 -c copy

FATE_FFMPEG += $(FATE_DASHENC_LAVFI-yes)
fate-dashenc: $(FATE_DASHENC_LAVFI-yes)
