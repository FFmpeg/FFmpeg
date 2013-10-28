FATE_LIBAVCODEC-$(CONFIG_GOLOMB) += fate-golomb
fate-golomb: libavcodec/golomb-test$(EXESUF)
fate-golomb: CMD = run libavcodec/golomb-test
fate-golomb: REF = /dev/null

FATE_LIBAVCODEC-$(CONFIG_DCT) += fate-idct8x8
fate-idct8x8: libavcodec/dct-test$(EXESUF)
fate-idct8x8: CMD = run libavcodec/dct-test -i
fate-idct8x8: CMP = null
fate-idct8x8: REF = /dev/null

FATE_LIBAVCODEC-yes += fate-iirfilter
fate-iirfilter: libavcodec/iirfilter-test$(EXESUF)
fate-iirfilter: CMD = run libavcodec/iirfilter-test

FATE_LIBAVCODEC-$(CONFIG_RANGECODER) += fate-rangecoder
fate-rangecoder: libavcodec/rangecoder-test$(EXESUF)
fate-rangecoder: CMD = run libavcodec/rangecoder-test
fate-rangecoder: CMP = null
fate-rangecoder: REF = /dev/null

FATE-$(CONFIG_AVCODEC) += $(FATE_LIBAVCODEC-yes)
fate-libavcodec: $(FATE_LIBAVCODEC-yes)
