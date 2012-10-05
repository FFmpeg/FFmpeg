FATE_LIBAVFORMAT += fate-url
fate-url: libavformat/url-test$(EXESUF)
fate-url: CMD = run libavformat/url-test

fate-libavformat: $(FATE_LIBAVFORMAT)
