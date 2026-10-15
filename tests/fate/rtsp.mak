FATE_RTSP_FFPROBE-$(call ALLYES, SDP_DEMUXER FILE_PROTOCOL) \
                               += fate-rtsp-sdp-sample-rate \
                                  fate-rtsp-sdp-sample-rate-leading-zero \
                                  fate-rtsp-sdp-sample-rate-negative \
                                  fate-rtsp-sdp-sample-rate-zero \
                                  fate-rtsp-sdp-sample-rate-int-max \
                                  fate-rtsp-sdp-sample-rate-above-int-max \
                                  fate-rtsp-sdp-sample-rate-overflow

# The RealNetworks "SampleRate:integer;" attribute overrides the rate the
# rtpmap line established. Every description below keeps the rtpmap rate at
# 44100 while the attribute asks for something else, so the probed rate tells
# whether the attribute was applied or ignored. custom_io keeps the demuxer
# from opening a transport, which keeps these tests offline.
SDP_SAMPLE_RATE_HEAD = v=0\no=- 0 0 IN IP4 127.0.0.1\ns=SampleRate\nc=IN IP4 127.0.0.1\nt=0 0\nm=audio 5004 RTP/AVP 97\na=rtpmap:97 L16/44100/1\n
SDP_SAMPLE_RATE_PROBE = run ffprobe$(PROGSSUF)$(EXESUF) -v error \
    -sdp_flags custom_io -protocol_whitelist file \
    -analyzeduration 0 -probesize 32 \
    -show_entries stream=sample_rate -print_format compact

define FATE_SDP_SAMPLE_RATE_CMD
    cleanfiles="$${cleanfiles} $${outfile}.sdp"; \
    printf "$(SDP_SAMPLE_RATE_HEAD)a=SampleRate:integer;$(1)\n" \
        > "$${outfile}.sdp"; \
    $(SDP_SAMPLE_RATE_PROBE) "$$(target_path "$${outfile}.sdp")"
endef

# A valid attribute must be applied, otherwise the tests below would pass
# without the attribute ever being parsed.
fate-rtsp-sdp-sample-rate: CMD = $(call FATE_SDP_SAMPLE_RATE_CMD,48000)

# A leading zero is part of a decimal rate, not an octal prefix.
fate-rtsp-sdp-sample-rate-leading-zero: CMD = $(call FATE_SDP_SAMPLE_RATE_CMD,048000)

# An invalid rate must leave the rtpmap rate in place.
fate-rtsp-sdp-sample-rate-negative: CMD = $(call FATE_SDP_SAMPLE_RATE_CMD,-1)
fate-rtsp-sdp-sample-rate-zero: CMD = $(call FATE_SDP_SAMPLE_RATE_CMD,0)
fate-rtsp-sdp-sample-rate-overflow: CMD = $(call FATE_SDP_SAMPLE_RATE_CMD,99999999999999999999)

# The two sides of the upper bound. Where long is wider than int, neither value
# sets ERANGE, so only the bound itself separates them.
fate-rtsp-sdp-sample-rate-int-max: CMD = $(call FATE_SDP_SAMPLE_RATE_CMD,2147483647)
fate-rtsp-sdp-sample-rate-above-int-max: CMD = $(call FATE_SDP_SAMPLE_RATE_CMD,2147483648)

FATE_FFPROBE += $(FATE_RTSP_FFPROBE-yes)

fate-rtsp: $(FATE_RTSP_FFPROBE-yes)
