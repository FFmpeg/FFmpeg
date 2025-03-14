FATE_OGG_VORBIS += fate-ogg-vorbis-chained-meta
fate-ogg-vorbis-chained-meta: REF = $(SRC_PATH)/tests/ref/fate/ogg-vorbis-chained-meta.txt
fate-ogg-vorbis-chained-meta: CMD = run $(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF) $(TARGET_SAMPLES)/ogg-vorbis/chained-meta.ogg

FATE_OGG_VORBIS += fate-ogg-vorbis-copy-chained-meta
fate-ogg-vorbis-copy-chained-meta: $(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF) $(FFMPEG)
fate-ogg-vorbis-copy-chained-meta: REF = $(SRC_PATH)/tests/ref/fate/ogg-vorbis-chained-meta.txt
fate-ogg-vorbis-copy-chained-meta: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel quiet -i $(TARGET_SAMPLES)/ogg-vorbis/chained-meta.ogg -c copy -f ogg -y" "$(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF)" ogg

FATE_OGG_VORBIS-$(call DEMDEC, OGG, VORBIS) += $(FATE_OGG_VORBIS)

FATE_SAMPLES_DUMP_STREAM_META += $(FATE_OGG_VORBIS-yes)

FATE_EXTERN += $(FATE_OGG_VORBIS-yes)

fate-ogg-vorbis: $(FATE_OGG_VORBIS-yes)
