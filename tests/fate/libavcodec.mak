FATE_LIBAVCODEC-yes += fate-avpacket
fate-avpacket: libavcodec/tests/avpacket$(EXESUF)
fate-avpacket: CMD = run libavcodec/tests/avpacket$(EXESUF)
fate-avpacket: CMP = null

FATE_LIBAVCODEC-$(CONFIG_CABAC) += fate-cabac
fate-cabac: libavcodec/tests/cabac$(EXESUF)
fate-cabac: CMD = run libavcodec/tests/cabac$(EXESUF)
fate-cabac: CMP = null

FATE_LIBAVCODEC-yes += fate-celp_math
fate-celp_math: libavcodec/tests/celp_math$(EXESUF)
fate-celp_math: CMD = run libavcodec/tests/celp_math$(EXESUF)
fate-celp_math: CMP = null

FATE_LIBAVCODEC-yes += fate-codec_desc
fate-codec_desc: libavcodec/tests/codec_desc$(EXESUF)
fate-codec_desc: CMD = run libavcodec/tests/codec_desc$(EXESUF)
fate-codec_desc: CMP = null

FATE_LIBAVCODEC-$(CONFIG_GOLOMB) += fate-golomb
fate-golomb: libavcodec/tests/golomb$(EXESUF)
fate-golomb: CMD = run libavcodec/tests/golomb$(EXESUF)
fate-golomb: CMP = null

FATE_LIBAVCODEC-$(CONFIG_IDCTDSP) += fate-idct8x8-0 fate-idct8x8-1 fate-idct8x8-2 fate-idct248

fate-idct8x8-0: libavcodec/tests/dct$(EXESUF)
fate-idct8x8-0: CMD = run libavcodec/tests/dct$(EXESUF) -i 0
fate-idct8x8-0: CMP = null

fate-idct8x8-1: libavcodec/tests/dct$(EXESUF)
fate-idct8x8-1: CMD = run libavcodec/tests/dct$(EXESUF) -i 1
fate-idct8x8-1: CMP = null

fate-idct8x8-2: libavcodec/tests/dct$(EXESUF)
fate-idct8x8-2: CMD = run libavcodec/tests/dct$(EXESUF) -i 2
fate-idct8x8-2: CMP = null

fate-idct248: libavcodec/tests/dct$(EXESUF)
fate-idct248: CMD = run libavcodec/tests/dct$(EXESUF) -4
fate-idct248: CMP = null

FATE_LIBAVCODEC-$(CONFIG_IDCTDSP) += fate-dct8x8
fate-dct8x8: libavcodec/tests/dct$(EXESUF)
fate-dct8x8: CMD = run libavcodec/tests/dct$(EXESUF)
fate-dct8x8: CMP = null

FATE_LIBAVCODEC-$(CONFIG_H264_METADATA_BSF) += fate-h264-levels
fate-h264-levels: libavcodec/tests/h264_levels$(EXESUF)
fate-h264-levels: CMD = run libavcodec/tests/h264_levels$(EXESUF)
fate-h264-levels: REF = /dev/null

FATE_LIBAVCODEC-$(CONFIG_HEVC_METADATA_BSF) += fate-h265-levels
fate-h265-levels: libavcodec/tests/h265_levels$(EXESUF)
fate-h265-levels: CMD = run libavcodec/tests/h265_levels$(EXESUF)
fate-h265-levels: REF = /dev/null

FATE_LIBAVCODEC-$(CONFIG_IIRFILTER) += fate-iirfilter
fate-iirfilter: libavcodec/tests/iirfilter$(EXESUF)
fate-iirfilter: CMD = run libavcodec/tests/iirfilter$(EXESUF)

FATE_LIBAVCODEC-$(CONFIG_MPEGVIDEO) += fate-mpeg12framerate
fate-mpeg12framerate: libavcodec/tests/mpeg12framerate$(EXESUF)
fate-mpeg12framerate: CMD = run libavcodec/tests/mpeg12framerate$(EXESUF)
fate-mpeg12framerate: REF = /dev/null

FATE_LIBAVCODEC-$(CONFIG_RANGECODER) += fate-rangecoder
fate-rangecoder: libavcodec/tests/rangecoder$(EXESUF)
fate-rangecoder: CMD = run libavcodec/tests/rangecoder$(EXESUF)
fate-rangecoder: CMP = null

FATE_LIBAVCODEC-yes += fate-mathops
fate-mathops: libavcodec/tests/mathops$(EXESUF)
fate-mathops: CMD = run libavcodec/tests/mathops$(EXESUF)
fate-mathops: CMP = null

FATE_LIBAVCODEC-$(CONFIG_JPEG2000_ENCODER) += fate-j2k-dwt
fate-j2k-dwt: libavcodec/tests/jpeg2000dwt$(EXESUF)
fate-j2k-dwt: CMD = run libavcodec/tests/jpeg2000dwt$(EXESUF)

FATE_LIBAVCODEC-yes += fate-libavcodec-avcodec
fate-libavcodec-avcodec: libavcodec/tests/avcodec$(EXESUF)
fate-libavcodec-avcodec: CMD = run libavcodec/tests/avcodec$(EXESUF)
fate-libavcodec-avcodec: CMP = null

FATE_LIBAVCODEC-yes += fate-libavcodec-huffman
fate-libavcodec-huffman: libavcodec/tests/mjpegenc_huffman$(EXESUF)
fate-libavcodec-huffman: CMD = run libavcodec/tests/mjpegenc_huffman$(EXESUF)
fate-libavcodec-huffman: CMP = null

FATE_LIBAVCODEC-yes += fate-libavcodec-htmlsubtitles
fate-libavcodec-htmlsubtitles: libavcodec/tests/htmlsubtitles$(EXESUF)
fate-libavcodec-htmlsubtitles: CMD = run libavcodec/tests/htmlsubtitles$(EXESUF)

FATE-$(CONFIG_AVCODEC) += $(FATE_LIBAVCODEC-yes)
fate-libavcodec: $(FATE_LIBAVCODEC-yes)
