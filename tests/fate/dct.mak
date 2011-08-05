FATE_TESTS += fate-idct8x8
fate-idct8x8: libavcodec/dct-test$(EXESUF)
fate-idct8x8: CMD = run libavcodec/dct-test -i
fate-idct8x8: REF = /dev/null
fate-idct8x8: CMP = null
