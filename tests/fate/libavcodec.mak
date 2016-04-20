FATE_LIBAVCODEC-$(CONFIG_GOLOMB) += fate-golomb
fate-golomb: libavcodec/tests/golomb$(EXESUF)
fate-golomb: CMD = run libavcodec/tests/golomb
fate-golomb: REF = /dev/null

FATE_LIBAVCODEC-$(CONFIG_IDCTDSP) += fate-idct8x8
fate-idct8x8: libavcodec/tests/dct$(EXESUF)
fate-idct8x8: CMD = run libavcodec/tests/dct -i
fate-idct8x8: CMP = null
fate-idct8x8: REF = /dev/null

FATE_LIBAVCODEC-$(CONFIG_IIRFILTER) += fate-iirfilter
fate-iirfilter: libavcodec/tests/iirfilter$(EXESUF)
fate-iirfilter: CMD = run libavcodec/tests/iirfilter

FATE_LIBAVCODEC-$(CONFIG_RANGECODER) += fate-rangecoder
fate-rangecoder: libavcodec/tests/rangecoder$(EXESUF)
fate-rangecoder: CMD = run libavcodec/tests/rangecoder
fate-rangecoder: CMP = null
fate-rangecoder: REF = /dev/null

FATE-$(CONFIG_AVCODEC) += $(FATE_LIBAVCODEC-yes)
fate-libavcodec: $(FATE_LIBAVCODEC-yes)
