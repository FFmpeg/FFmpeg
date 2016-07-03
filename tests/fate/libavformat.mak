#FATE_LIBAVFORMAT-$(HAVE_PTHREADS) += fate-async
#fate-async: libavformat/tests/async$(EXESUF)
#fate-async: CMD = run libavformat/tests/async

FATE_LIBAVFORMAT-$(CONFIG_NETWORK) += fate-noproxy
fate-noproxy: libavformat/tests/noproxy$(EXESUF)
fate-noproxy: CMD = run libavformat/tests/noproxy

FATE_LIBAVFORMAT-$(CONFIG_FFRTMPCRYPT_PROTOCOL) += fate-rtmpdh
fate-rtmpdh: libavformat/tests/rtmpdh$(EXESUF)
fate-rtmpdh: CMD = run libavformat/tests/rtmpdh

FATE_LIBAVFORMAT-$(CONFIG_SRTP) += fate-srtp
fate-srtp: libavformat/tests/srtp$(EXESUF)
fate-srtp: CMD = run libavformat/tests/srtp

FATE_LIBAVFORMAT-yes += fate-url
fate-url: libavformat/tests/url$(EXESUF)
fate-url: CMD = run libavformat/tests/url

FATE_LIBAVFORMAT-$(CONFIG_MOV_MUXER) += fate-movenc
fate-movenc: libavformat/tests/movenc$(EXESUF)
fate-movenc: CMD = run libavformat/tests/movenc

FATE_LIBAVFORMAT += $(FATE_LIBAVFORMAT-yes)
FATE-$(CONFIG_AVFORMAT) += $(FATE_LIBAVFORMAT)
fate-libavformat: $(FATE_LIBAVFORMAT)
