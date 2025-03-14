FATE_OGG_OPUS += fate-ogg-opus-chained-meta
fate-ogg-opus-chained-meta: REF = $(SRC_PATH)/tests/ref/fate/ogg-opus-chained-meta.txt
fate-ogg-opus-chained-meta: CMD = run $(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF) $(TARGET_SAMPLES)/ogg-opus/chained-meta.ogg

FATE_OGG_OPUS += fate-ogg-opus-copy-chained-meta
fate-ogg-opus-copy-chained-meta: $(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF) $(FFMPEG)
fate-ogg-opus-copy-chained-meta: REF = $(SRC_PATH)/tests/ref/fate/ogg-opus-chained-meta.txt
fate-ogg-opus-copy-chained-meta: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel quiet -i $(TARGET_SAMPLES)/ogg-opus/chained-meta.ogg -c copy -f ogg -y" "$(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF)" ogg

FATE_OGG_OPUS-$(call DEMDEC, OGG, OPUS) += $(FATE_OGG_OPUS)

FATE_SAMPLES_DUMP_STREAM_META += $(FATE_OGG_OPUS-yes)

FATE_EXTERN += $(FATE_OGG_OPUS-yes)

fate-ogg-opus: $(FATE_OGG_OPUS-yes)
