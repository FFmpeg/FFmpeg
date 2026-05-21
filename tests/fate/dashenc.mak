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

FATE_FFMPEG += $(FATE_DASHENC_LAVFI-yes)
fate-dashenc: $(FATE_DASHENC_LAVFI-yes)
