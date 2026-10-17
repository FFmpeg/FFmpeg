#FATE_LIBAVFORMAT-$(HAVE_PTHREADS) += fate-async
#fate-async: libavformat/tests/async$(EXESUF)
#fate-async: CMD = run libavformat/tests/async

FATE_LIBAVFORMAT-$(CONFIG_MP3_MUXER) += fate-id3v2enc
fate-id3v2enc: libavformat/tests/id3v2enc$(EXESUF)
fate-id3v2enc: CMD = run libavformat/tests/id3v2enc$(EXESUF)

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

FATE_LIBAVFORMAT-$(CONFIG_NETWORK) += fate-noproxy
fate-noproxy: libavformat/tests/noproxy$(EXESUF)
fate-noproxy: CMD = run libavformat/tests/noproxy$(EXESUF)

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

tests/data/hls-parse-master.m3u8: TAG = GEN
tests/data/hls-parse-master.m3u8: | tests/data
	$(Q)printf '#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=64000\na.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=32000\nb.m3u8\n' > $@

tests/data/hls-parse-master-start.m3u8: TAG = GEN
tests/data/hls-parse-master-start.m3u8: | tests/data
	$(Q)printf '#EXTM3U\n#EXT-X-START:TIME-OFFSET=1.5\n#EXT-X-STREAM-INF:BANDWIDTH=64000\na.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=32000\nb.m3u8\n' > $@

tests/data/hls-parse-media.m3u8: TAG = GEN
tests/data/hls-parse-media.m3u8: | tests/data
	$(Q)printf '#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nseg0.ts\n#EXTINF:2,\nseg1.ts\n#EXTINF:2,\nseg2.ts\n' > $@

tests/data/hls-parse-media-start.m3u8: TAG = GEN
tests/data/hls-parse-media-start.m3u8: | tests/data
	$(Q)printf '#EXTM3U\n#EXT-X-START:TIME-OFFSET=4\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nseg0.ts\n#EXTINF:2,\nseg1.ts\n#EXTINF:2,\nseg2.ts\n' > $@

tests/data/hls-parse-media-start-endlist.m3u8: TAG = GEN
tests/data/hls-parse-media-start-endlist.m3u8: | tests/data
	$(Q)printf '#EXTM3U\n#EXT-X-START:TIME-OFFSET=4\n#EXT-X-ENDLIST\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nseg0.ts\n#EXTINF:2,\nseg1.ts\n#EXTINF:2,\nseg2.ts\n' > $@

FATE_HLS_PARSE = fate-hls-parse-media-start           \
                 fate-hls-parse-media-start-endlist   \
                 fate-hls-parse-master-start          \
                 fate-hls-parse-master-start-media    \
                 fate-hls-parse-master-start-override \
                 fate-hls-parse-master-media-start

$(FATE_HLS_PARSE): libavformat/tests/hls$(EXESUF)
fate-hls-parse-media-start: tests/data/hls-parse-media-start.m3u8
fate-hls-parse-media-start: CMD = run libavformat/tests/hls$(EXESUF) $(TARGET_PATH)/tests/data/hls-parse-media-start.m3u8
fate-hls-parse-media-start-endlist: tests/data/hls-parse-media-start-endlist.m3u8
fate-hls-parse-media-start-endlist: CMD = run libavformat/tests/hls$(EXESUF) $(TARGET_PATH)/tests/data/hls-parse-media-start-endlist.m3u8
fate-hls-parse-master-start: tests/data/hls-parse-master-start.m3u8
fate-hls-parse-master-start: CMD = run libavformat/tests/hls$(EXESUF) $(TARGET_PATH)/tests/data/hls-parse-master-start.m3u8
fate-hls-parse-master-start-media: tests/data/hls-parse-master-start.m3u8 tests/data/hls-parse-media.m3u8
fate-hls-parse-master-start-media: CMD = run libavformat/tests/hls$(EXESUF) $(TARGET_PATH)/tests/data/hls-parse-master-start.m3u8 $(TARGET_PATH)/tests/data/hls-parse-media.m3u8
fate-hls-parse-master-start-override: tests/data/hls-parse-master-start.m3u8 tests/data/hls-parse-media-start.m3u8
fate-hls-parse-master-start-override: CMD = run libavformat/tests/hls$(EXESUF) $(TARGET_PATH)/tests/data/hls-parse-master-start.m3u8 $(TARGET_PATH)/tests/data/hls-parse-media-start.m3u8
fate-hls-parse-master-media-start: tests/data/hls-parse-master.m3u8 tests/data/hls-parse-media-start.m3u8
fate-hls-parse-master-media-start: CMD = run libavformat/tests/hls$(EXESUF) $(TARGET_PATH)/tests/data/hls-parse-master.m3u8 $(TARGET_PATH)/tests/data/hls-parse-media-start.m3u8

FATE_LIBAVFORMAT-$(call ALLYES, HLS_DEMUXER FILE_PROTOCOL) += $(FATE_HLS_PARSE)

FATE_LIBAVFORMAT-$(CONFIG_IMF_DEMUXER) += fate-imf
fate-imf: libavformat/tests/imf$(EXESUF)
fate-imf: CMD = run libavformat/tests/imf$(EXESUF)

FATE_LIBAVFORMAT += fate-seek_utils
fate-seek_utils: libavformat/tests/seek_utils$(EXESUF)
fate-seek_utils: CMD = run libavformat/tests/seek_utils$(EXESUF)
fate-seek_utils: CMP = null

FATE_LIBAVFORMAT += $(FATE_LIBAVFORMAT-yes)
FATE-$(CONFIG_AVFORMAT) += $(FATE_LIBAVFORMAT)
fate-libavformat: $(FATE_LIBAVFORMAT)
