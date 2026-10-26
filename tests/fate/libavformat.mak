FATE_LIBAVFORMAT += fate-codecstring
fate-codecstring: libavformat/tests/codecstring$(EXESUF)
fate-codecstring: CMD = run libavformat/tests/codecstring$(EXESUF)

FATE_LIBAVFORMAT += fate-demux-probe
fate-demux-probe: libavformat/tests/demux_probe$(EXESUF)
fate-demux-probe: CMD = run libavformat/tests/demux_probe$(EXESUF)

#FATE_LIBAVFORMAT-$(HAVE_PTHREADS) += fate-async
#fate-async: libavformat/tests/async$(EXESUF)
#fate-async: CMD = run libavformat/tests/async

FATE_LIBAVFORMAT-$(CONFIG_MP3_MUXER) += fate-id3v2enc
fate-id3v2enc: libavformat/tests/id3v2enc$(EXESUF)
fate-id3v2enc: CMD = run libavformat/tests/id3v2enc$(EXESUF)

FATE_LIBAVFORMAT-$(CONFIG_MP3_MUXER) += fate-id3v2chap
fate-id3v2chap: libavformat/tests/id3v2chap$(EXESUF)
fate-id3v2chap: CMD = run libavformat/tests/id3v2chap$(EXESUF)

FATE_LIBAVFORMAT-$(call ALLYES, AU_MUXER AU_DEMUXER) += fate-auenc
fate-auenc: libavformat/tests/auenc$(EXESUF)
fate-auenc: CMD = run libavformat/tests/auenc$(EXESUF)

FATE_LIBAVFORMAT += fate-mkdir
fate-mkdir: libavformat/tests/mkdir$(EXESUF)
fate-mkdir: CMD = run libavformat/tests/mkdir$(EXESUF)
fate-mkdir: CMP = null

FATE_LIBAVFORMAT += fate-rename
fate-rename: libavformat/tests/rename$(EXESUF)
fate-rename: CMD = run libavformat/tests/rename$(EXESUF)
fate-rename: CMP = null

FATE_HTTP_STATUS_LINES = \
    "HTTP/1.1 200 OK"                    \
    "HTTP/1.0 200 OK"                    \
    "HTTP/1.1 404 Not Found"             \
    "HTTP/1.1 500 Internal Server Error" \
    "HTTP/1.1 204 No Content"            \
    "HTTP/1.1 200"                       \
    "HTTP/1.1"                           \
    "HTTP/1.1 OK"                        \
    "HTTP"                               \
    "200 OK"                             \
    "NOT A STATUS LINE"                  \
    ""                                   \
    "ICY 200 OK"                         \
    "ICY 404 Not Found"                  \
    "HTTP/1.1 200OK"                     \
    "HTTP/1.1 +200 Plus"                 \
    "HTTP/1.1 20 Short"                  \
    "HTTP/1.1 2000 Four"                 \
    "HTTP/x 200 Bad version"             \
    "http/1.1 200 lowercase"             \
    "HTTP/1.1 099 Too low"               \
    "HTTP/1.1 600 Too high"

FATE_LIBAVFORMAT-$(CONFIG_HTTP_PROTOCOL) += fate-http-status-line
fate-http-status-line: libavformat/tests/http$(EXESUF)
fate-http-status-line: CMD = run libavformat/tests/http$(EXESUF) $(FATE_HTTP_STATUS_LINES)

FATE_LIBAVFORMAT-$(CONFIG_HTTP_PROTOCOL) += fate-httpcookie
fate-httpcookie: libavformat/tests/httpcookie$(EXESUF)
fate-httpcookie: CMD = run libavformat/tests/httpcookie$(EXESUF)

FATE_LIBAVFORMAT-$(CONFIG_NETWORK) += fate-noproxy
fate-noproxy: libavformat/tests/noproxy$(EXESUF)
fate-noproxy: CMD = run libavformat/tests/noproxy$(EXESUF)

# HEAD responses have no body, and CONNECT tunnels are unbounded streams.
# Self-contained loopback test; it needs no samples and no external network.
FATE_HTTP_NOBODY-$(CONFIG_HTTP_PROTOCOL)      += fate-http-head-no-body
FATE_HTTP_NOBODY-$(CONFIG_HTTPPROXY_PROTOCOL) += fate-httpproxy-tunnel-eof
FATE_HTTP_NOBODY-$(CONFIG_LIBCURL_PROTOCOL)   += fate-http-curl-head
FATE_HTTP_NOBODY_RUN-$(HAVE_LOOPBACK)         += $(FATE_HTTP_NOBODY-yes)
FATE_LIBAVFORMAT-$(HAVE_PTHREADS)             += $(FATE_HTTP_NOBODY_RUN-yes)

fate-http-head-no-body: libavformat/tests/http_nobody$(EXESUF)
fate-http-head-no-body: CMD = run libavformat/tests/http_nobody$(EXESUF) http

fate-httpproxy-tunnel-eof: libavformat/tests/http_nobody$(EXESUF)
fate-httpproxy-tunnel-eof: CMD = run libavformat/tests/http_nobody$(EXESUF) proxy

fate-http-curl-head: libavformat/tests/http_nobody$(EXESUF)
fate-http-curl-head: CMD = no_proxy=127.0.0.1,localhost NO_PROXY=127.0.0.1,localhost run libavformat/tests/http_nobody$(EXESUF) libcurl

FATE_LIBAVFORMAT-$(CONFIG_FFRTMPCRYPT_PROTOCOL) += fate-rtmpdh
fate-rtmpdh: libavformat/tests/rtmpdh$(EXESUF)
fate-rtmpdh: CMD = run libavformat/tests/rtmpdh$(EXESUF)

FATE_LIBAVFORMAT-$(CONFIG_RTPDEC) += fate-rtpdec
fate-rtpdec: libavformat/tests/rtpdec$(EXESUF)
fate-rtpdec: CMD = run libavformat/tests/rtpdec$(EXESUF)

FATE_LIBAVFORMAT-$(CONFIG_SRTP) += fate-srtp
fate-srtp: libavformat/tests/srtp$(EXESUF)
fate-srtp: CMD = run libavformat/tests/srtp$(EXESUF)

FATE_LIBAVFORMAT-yes += fate-url
fate-url: libavformat/tests/url$(EXESUF)
fate-url: CMD = run libavformat/tests/url$(EXESUF)

FATE_LIBAVFORMAT-$(call ALLYES, HLS_MUXER MP4_MUXER) += fate-hlsenc-filenames
fate-hlsenc-filenames: libavformat/tests/hlsenc$(EXESUF)
fate-hlsenc-filenames: CMD = run libavformat/tests/hlsenc$(EXESUF)

FATE_LIBAVFORMAT-$(call ALLYES, HLS_MUXER MP4_MUXER FILE_PROTOCOL) += fate-hlsenc-file-filenames
fate-hlsenc-file-filenames: libavformat/tests/hlsenc$(EXESUF)
fate-hlsenc-file-filenames: CMD = run libavformat/tests/hlsenc$(EXESUF) file

FATE_LIBAVFORMAT-$(CONFIG_IMF_DEMUXER) += fate-imf
fate-imf: libavformat/tests/imf$(EXESUF)
fate-imf: CMD = run libavformat/tests/imf$(EXESUF)

FATE_LIBAVFORMAT-$(CONFIG_MPEGTS_DEMUXER) += fate-mpegts-probe-pid
fate-mpegts-probe-pid: libavformat/tests/mpegts_probe$(EXESUF)
fate-mpegts-probe-pid: CMD = run libavformat/tests/mpegts_probe$(EXESUF)

FATE_LIBAVFORMAT += fate-seek_utils
fate-seek_utils: libavformat/tests/seek_utils$(EXESUF)
fate-seek_utils: CMD = run libavformat/tests/seek_utils$(EXESUF)

FATE_LIBAVFORMAT_SAMPLES-$(call ALLYES, AVFORMAT AC3_PARSER AC3_DECODER) += fate-parse-skip-samples
fate-parse-skip-samples: libavformat/tests/parse_skip_samples$(EXESUF)
fate-parse-skip-samples: CMD = run libavformat/tests/parse_skip_samples$(EXESUF) $(TARGET_SAMPLES)/ac3/millers_crossing_4.0.ac3

FATE_EXTERN += $(FATE_LIBAVFORMAT_SAMPLES-yes)

FATE_LIBAVFORMAT += $(FATE_LIBAVFORMAT-yes)
FATE-$(CONFIG_AVFORMAT) += $(FATE_LIBAVFORMAT)
fate-libavformat: $(FATE_LIBAVFORMAT) $(if $(SAMPLES),$(FATE_LIBAVFORMAT_SAMPLES-yes))
