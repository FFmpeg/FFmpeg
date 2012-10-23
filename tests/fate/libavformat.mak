FATE_LIBAVFORMAT += fate-url
fate-url: libavformat/url-test$(EXESUF)
fate-url: CMD = run libavformat/url-test

FATE-$(CONFIG_AVFORMAT) += $(FATE_LIBAVFORMAT)
fate-libavformat: $(FATE_LIBAVFORMAT)
