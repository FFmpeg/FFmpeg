FATE_API_LIBAVCODEC-$(call ENCDEC, FLAC, FLAC) += fate-api-flac
fate-api-flac: $(APITESTSDIR)/api-flac-test$(EXESUF)
fate-api-flac: CMD = run $(APITESTSDIR)/api-flac-test
fate-api-flac: CMP = null
fate-api-flac: REF = /dev/null

FATE-$(CONFIG_AVCODEC) += $(FATE_API_LIBAVCODEC-yes)
fate-api: $(FATE_API_LIBAVCODEC-yes)
