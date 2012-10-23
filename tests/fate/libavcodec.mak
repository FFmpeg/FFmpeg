FATE_LIBAVCODEC += fate-golomb
fate-golomb: libavcodec/golomb-test$(EXESUF)
fate-golomb: CMD = run libavcodec/golomb-test
fate-golomb: REF = /dev/null

FATE_LIBAVCODEC += fate-iirfilter
fate-iirfilter: libavcodec/iirfilter-test$(EXESUF)
fate-iirfilter: CMD = run libavcodec/iirfilter-test

FATE_LIBAVCODEC += fate-rangecoder
fate-rangecoder: libavcodec/rangecoder-test$(EXESUF)
fate-rangecoder: CMD = run libavcodec/rangecoder-test
fate-rangecoder: CMP = null
fate-rangecoder: REF = /dev/null

FATE-$(CONFIG_AVCODEC) += $(FATE_LIBAVCODEC)
fate-libavcodec: $(FATE_LIBAVCODEC)
