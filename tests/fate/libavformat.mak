FATE_LIBAVFORMAT += fate-noproxy
fate-noproxy: libavformat/noproxy-test$(EXESUF)
fate-noproxy: CMD = run libavformat/noproxy-test

FATE_LIBAVFORMAT += fate-srtp
fate-srtp: libavformat/srtp-test$(EXESUF)
fate-srtp: CMD = run libavformat/srtp-test

FATE_LIBAVFORMAT += fate-url
fate-url: libavformat/url-test$(EXESUF)
fate-url: CMD = run libavformat/url-test

FATE-$(CONFIG_AVFORMAT) += $(FATE_LIBAVFORMAT)
fate-libavformat: $(FATE_LIBAVFORMAT)
