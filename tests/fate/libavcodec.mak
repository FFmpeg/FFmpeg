FATE_TESTS += fate-iirfilter
fate-iirfilter: libavcodec/iirfilter-test$(EXESUF)
fate-iirfilter: CMD = run libavcodec/iirfilter-test
