FATE_API_LIBAVCODEC-$(call ENCDEC, FLAC, FLAC) += fate-api-flac
fate-api-flac: $(APITESTSDIR)/api-flac-test$(EXESUF)
fate-api-flac: CMD = run $(APITESTSDIR)/api-flac-test
fate-api-flac: CMP = null
fate-api-flac: REF = /dev/null

FATE_API_SAMPLES-LIBAVFORMAT-$(call DEMDEC, H264, H264) += fate-api-h264
fate-api-h264: $(APITESTSDIR)/api-h264-test$(EXESUF)
fate-api-h264: CMD = run $(APITESTSDIR)/api-h264-test $(TARGET_SAMPLES)/h264-conformance/SVA_NL2_E.264

FATE-$(CONFIG_AVCODEC) += $(FATE_API_LIBAVCODEC-yes)
FATE_API_SAMPLES-$(CONFIG_AVFORMAT) += $(FATE_API_SAMPLES-LIBAVFORMAT-yes)
FATE_SAMPLES_AVCONV += $(FATE_API_SAMPLES-yes)

fate-api: $(FATE_API_LIBAVCODEC-yes) $(FATE_API_SAMPLES-yes)
