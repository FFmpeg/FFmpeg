FATE_LIBAVFORMAT-$(CONFIG_NETWORK) += fate-noproxy
fate-noproxy: libavformat/noproxy-test$(EXESUF)
fate-noproxy: CMD = run libavformat/noproxy-test

FATE_LIBAVFORMAT-$(CONFIG_FFRTMPCRYPT_PROTOCOL) += fate-rtmpdh
fate-rtmpdh: libavformat/rtmpdh-test$(EXESUF)
fate-rtmpdh: CMD = run libavformat/rtmpdh-test

FATE_LIBAVFORMAT-yes += fate-srtp
fate-srtp: libavformat/srtp-test$(EXESUF)
fate-srtp: CMD = run libavformat/srtp-test

FATE_LIBAVFORMAT-yes += fate-url
fate-url: libavformat/url-test$(EXESUF)
fate-url: CMD = run libavformat/url-test

FATE_SAMPLES_AVCONV-$(call DEMDEC, H264, H264) += fate-api-h264
fate-api-h264: libavformat/api-h264-test$(EXESUF)
fate-api-h264: CMD = run libavformat/api-h264-test $(TARGET_SAMPLES)/h264-conformance/SVA_NL2_E.264

FATE-$(CONFIG_AVFORMAT) += $(FATE_LIBAVFORMAT-yes)
fate-libavformat: $(FATE_LIBAVFORMAT)
