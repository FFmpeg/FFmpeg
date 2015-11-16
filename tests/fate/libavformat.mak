FATE_LIBAVFORMAT-$(HAVE_PTHREADS) += fate-async
fate-async: libavformat/async-test$(EXESUF)
fate-async: CMD = run libavformat/async-test

FATE_LIBAVFORMAT-$(CONFIG_NETWORK) += fate-noproxy
fate-noproxy: libavformat/noproxy-test$(EXESUF)
fate-noproxy: CMD = run libavformat/noproxy-test

FATE_LIBAVFORMAT-$(CONFIG_FFRTMPCRYPT_PROTOCOL) += fate-rtmpdh
fate-rtmpdh: libavformat/rtmpdh-test$(EXESUF)
fate-rtmpdh: CMD = run libavformat/rtmpdh-test

FATE_LIBAVFORMAT-yes += fate-srtp
fate-srtp: libavformat/srtp-test$(EXESUF)
fate-srtp: CMD = run libavformat/srtp-test

FATE_LIBAVFORMAT-yes += fate-url
fate-url: libavformat/url-test$(EXESUF)
fate-url: CMD = run libavformat/url-test

FATE_LIBAVFORMAT-$(CONFIG_MOV_MUXER) += fate-movenc
fate-movenc: libavformat/movenc-test$(EXESUF)
fate-movenc: CMD = run libavformat/movenc-test

FATE-$(CONFIG_AVFORMAT) += $(FATE_LIBAVFORMAT-yes)
fate-libavformat: $(FATE_LIBAVFORMAT)
