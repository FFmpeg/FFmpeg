FATE_LIBAVCODEC-yes += fate-avpacket
fate-avpacket: libavcodec/tests/avpacket$(EXESUF)
fate-avpacket: CMD = run libavcodec/tests/avpacket
fate-avpacket: REF = /dev/null

FATE_LIBAVCODEC-$(CONFIG_CABAC) += fate-cabac
fate-cabac: libavcodec/tests/cabac$(EXESUF)
fate-cabac: CMD = run libavcodec/tests/cabac
fate-cabac: REF = /dev/null

FATE_LIBAVCODEC-yes += fate-celp_math
fate-celp_math: libavcodec/tests/celp_math$(EXESUF)
fate-celp_math: CMD = run libavcodec/tests/celp_math
fate-celp_math: REF = /dev/null

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

FATE_LIBAVCODEC-yes += fate-libavcodec-options
fate-libavcodec-options: libavcodec/tests/options$(EXESUF)
fate-libavcodec-options: CMD = run libavcodec/tests/options

FATE_LIBAVCODEC-$(CONFIG_RANGECODER) += fate-rangecoder
fate-rangecoder: libavcodec/tests/rangecoder$(EXESUF)
fate-rangecoder: CMD = run libavcodec/tests/rangecoder
fate-rangecoder: CMP = null
fate-rangecoder: REF = /dev/null

FATE_LIBAVCODEC-yes += fate-mathops
fate-mathops: libavcodec/tests/mathops$(EXESUF)
fate-mathops: CMD = run libavcodec/tests/mathops
fate-mathops: CMP = null
fate-mathops: REF = /dev/null

FATE_LIBAVCODEC-$(CONFIG_JPEG2000_ENCODER) += fate-j2k-dwt
fate-j2k-dwt: libavcodec/tests/jpeg2000dwt$(EXESUF)
fate-j2k-dwt: CMD = run libavcodec/tests/jpeg2000dwt

FATE_LIBAVCODEC-yes += fate-libavcodec-utils
fate-libavcodec-utils: libavcodec/tests/utils$(EXESUF)
fate-libavcodec-utils: CMD = run libavcodec/tests/utils
fate-libavcodec-utils: CMP = null
fate-libavcodec-utils: REF = /dev/null

FATE_LIBAVCODEC-yes += fate-libavcodec-huffman
fate-libavcodec-huffman: libavcodec/tests/mjpegenc_huffman$(EXESUF)
fate-libavcodec-huffman: CMD = run libavcodec/tests/mjpegenc_huffman
fate-libavcodec-huffman: CMP = null
fate-libavcodec-huffman: REF = /dev/null

FATE-$(CONFIG_AVCODEC) += $(FATE_LIBAVCODEC-yes)
fate-libavcodec: $(FATE_LIBAVCODEC-yes)
