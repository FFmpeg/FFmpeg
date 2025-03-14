FATE_OGG_FLAC += fate-ogg-flac-chained-meta
fate-ogg-flac-chained-meta: REF = $(SRC_PATH)/tests/ref/fate/ogg-flac-chained-meta.txt
fate-ogg-flac-chained-meta: CMD = run $(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF) $(TARGET_SAMPLES)/ogg-flac/chained-meta.ogg

FATE_OGG_FLAC += fate-ogg-flac-copy-chained-meta
fate-ogg-flac-copy-chained-meta: $(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF) $(FFMPEG)
fate-ogg-flac-copy-chained-meta: REF = $(SRC_PATH)/tests/ref/fate/ogg-flac-chained-meta.txt
fate-ogg-flac-copy-chained-meta: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel quiet -i $(TARGET_SAMPLES)/ogg-flac/chained-meta.ogg -c copy -f ogg -y" "$(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF)" ogg

FATE_OGG_FLAC-$(call DEMDEC, OGG, FLAC, FLAC_PARSER) += $(FATE_OGG_FLAC)

FATE_SAMPLES_DUMP_STREAM_META += $(FATE_OGG_FLAC-yes)

FATE_EXTERN += $(FATE_OGG_FLAC-yes)

fate-ogg-flac: $(FATE_OGG_FLAC-yes)
