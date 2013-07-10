FATE_LIBAVFORMAT-$(CONFIG_NETWORK) += fate-noproxy
fate-noproxy: libavformat/noproxy-test$(EXESUF)
fate-noproxy: CMD = run libavformat/noproxy-test

FATE_LIBAVFORMAT-yes += fate-srtp
fate-srtp: libavformat/srtp-test$(EXESUF)
fate-srtp: CMD = run libavformat/srtp-test

FATE_LIBAVFORMAT-yes += fate-url
fate-url: libavformat/url-test$(EXESUF)
fate-url: CMD = run libavformat/url-test

FATE-$(CONFIG_AVFORMAT) += $(FATE_LIBAVFORMAT-yes)
fate-libavformat: $(FATE_LIBAVFORMAT)
