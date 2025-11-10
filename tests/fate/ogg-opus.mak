FATE_OGG_OPUS += fate-ogg-opus-chained-meta
fate-ogg-opus-chained-meta: REF = $(SRC_PATH)/tests/ref/fate/ogg-opus-chained-meta.txt
fate-ogg-opus-chained-meta: CMD = run $(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF) $(TARGET_SAMPLES)/ogg-opus/chained-meta.ogg

FATE_OGG_OPUS-$(call DEMDEC, OGG, OPUS) += $(FATE_OGG_OPUS)

FATE_SAMPLES_DUMP_STREAM_META += $(FATE_OGG_OPUS-yes)

FATE_EXTERN += $(FATE_OGG_OPUS-yes)

fate-ogg-opus: $(FATE_OGG_OPUS-yes)
