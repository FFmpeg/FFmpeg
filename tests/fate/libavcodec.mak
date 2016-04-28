FATE_LIBAVCODEC-$(CONFIG_CABAC) += fate-cabac
fate-cabac: libavcodec/cabac-test$(EXESUF)
fate-cabac: CMD = run libavcodec/cabac-test
fate-cabac: REF = /dev/null

FATE_LIBAVCODEC-$(CONFIG_GOLOMB) += fate-golomb
fate-golomb: libavcodec/golomb-test$(EXESUF)
fate-golomb: CMD = run libavcodec/golomb-test
fate-golomb: REF = /dev/null

FATE_LIBAVCODEC-$(CONFIG_IDCTDSP) += fate-idct8x8
fate-idct8x8: libavcodec/dct-test$(EXESUF)
fate-idct8x8: CMD = run libavcodec/dct-test -i
fate-idct8x8: CMP = null
fate-idct8x8: REF = /dev/null

FATE_LIBAVCODEC-$(CONFIG_IIRFILTER) += fate-iirfilter
fate-iirfilter: libavcodec/iirfilter-test$(EXESUF)
fate-iirfilter: CMD = run libavcodec/iirfilter-test

FATE_LIBAVCODEC-yes += fate-libavcodec-options
fate-libavcodec-options: libavcodec/options-test$(EXESUF)
fate-libavcodec-options: CMD = run libavcodec/options-test

FATE_LIBAVCODEC-$(CONFIG_RANGECODER) += fate-rangecoder
fate-rangecoder: libavcodec/rangecoder-test$(EXESUF)
fate-rangecoder: CMD = run libavcodec/rangecoder-test
fate-rangecoder: CMP = null
fate-rangecoder: REF = /dev/null

FATE_LIBAVCODEC-yes += fate-mathops
fate-mathops: libavcodec/mathops-test$(EXESUF)
fate-mathops: CMD = run libavcodec/mathops-test
fate-mathops: CMP = null
fate-mathops: REF = /dev/null

FATE_LIBAVCODEC-$(CONFIG_JPEG2000_ENCODER) += fate-j2k-dwt
fate-j2k-dwt: libavcodec/jpeg2000dwt-test$(EXESUF)
fate-j2k-dwt: CMD = run libavcodec/jpeg2000dwt-test

FATE_LIBAVCODEC-yes += fate-libavcodec-utils
fate-libavcodec-utils: libavcodec/utils-test$(EXESUF)
fate-libavcodec-utils: CMD = run libavcodec/utils-test
fate-libavcodec-utils: CMP = null
fate-libavcodec-utils: REF = /dev/null

FATE-$(CONFIG_AVCODEC) += $(FATE_LIBAVCODEC-yes)
fate-libavcodec: $(FATE_LIBAVCODEC-yes)
