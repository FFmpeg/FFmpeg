FATE_TESTS += fate-golomb
fate-golomb: libavcodec/golomb-test$(EXESUF)
fate-golomb: CMD = run libavcodec/golomb-test
fate-golomb: REF = /dev/null

FATE_TESTS += fate-iirfilter
fate-iirfilter: libavcodec/iirfilter-test$(EXESUF)
fate-iirfilter: CMD = run libavcodec/iirfilter-test
