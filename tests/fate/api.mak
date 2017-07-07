FATE_API_LIBAVCODEC-$(call ENCDEC, FLAC, FLAC) += fate-api-flac
fate-api-flac: $(APITESTSDIR)/api-flac-test$(EXESUF)
fate-api-flac: CMD = run $(APITESTSDIR)/api-flac-test
fate-api-flac: CMP = null
fate-api-flac: REF = /dev/null

FATE_API_SAMPLES_LIBAVFORMAT-$(call DEMDEC, FLV, FLV) += fate-api-band
fate-api-band: $(APITESTSDIR)/api-band-test$(EXESUF)
fate-api-band: CMD = run $(APITESTSDIR)/api-band-test $(TARGET_SAMPLES)/mpeg4/resize_down-up.h263
fate-api-band: CMP = null
fate-api-band: REF = /dev/null

FATE_API_SAMPLES_LIBAVFORMAT-$(call DEMDEC, H264, H264) += fate-api-h264
fate-api-h264: $(APITESTSDIR)/api-h264-test$(EXESUF)
fate-api-h264: CMD = run $(APITESTSDIR)/api-h264-test $(TARGET_SAMPLES)/h264-conformance/SVA_NL2_E.264

FATE_API_LIBAVFORMAT-$(call DEMDEC, FLV, FLV) += fate-api-seek
fate-api-seek: $(APITESTSDIR)/api-seek-test$(EXESUF) fate-lavf-flv_fmt
fate-api-seek: CMD = run $(APITESTSDIR)/api-seek-test $(TARGET_PATH)/tests/data/lavf/lavf.flv 0 720
fate-api-seek: CMP = null
fate-api-seek: REF = /dev/null

FATE_API_SAMPLES_LIBAVFORMAT-$(call DEMDEC, IMAGE2, PNG) += fate-api-png-codec-param
fate-api-png-codec-param: $(APITESTSDIR)/api-codec-param-test$(EXESUF)
fate-api-png-codec-param: CMD = run $(APITESTSDIR)/api-codec-param-test $(TARGET_SAMPLES)/png1/lena-rgba.png

FATE_API_SAMPLES_LIBAVFORMAT-$(call DEMDEC, IMAGE2, MJPEG) += fate-api-mjpeg-codec-param
fate-api-mjpeg-codec-param: $(APITESTSDIR)/api-codec-param-test$(EXESUF)
fate-api-mjpeg-codec-param: CMD = run $(APITESTSDIR)/api-codec-param-test $(TARGET_SAMPLES)/exif/image_small.jpg

FATE_API-$(HAVE_THREADS) += fate-api-threadmessage
fate-api-threadmessage: $(APITESTSDIR)/api-threadmessage-test$(EXESUF)
fate-api-threadmessage: CMD = run $(APITESTSDIR)/api-threadmessage-test 3 10 30 50 2 20 40
fate-api-threadmessage: CMP = null
fate-api-threadmessage: REF = /dev/null

FATE_API_SAMPLES-$(CONFIG_AVFORMAT) += $(FATE_API_SAMPLES_LIBAVFORMAT-yes)

ifdef SAMPLES
    FATE_API_SAMPLES += $(FATE_API_SAMPLES-yes)
endif

FATE_API-$(CONFIG_AVCODEC) += $(FATE_API_LIBAVCODEC-yes)
FATE_API-$(CONFIG_AVFORMAT) += $(FATE_API_LIBAVFORMAT-yes)
FATE_API = $(FATE_API-yes)

FATE-yes += $(FATE_API) $(FATE_API_SAMPLES)

fate-api: $(FATE_API) $(FATE_API_SAMPLES)
