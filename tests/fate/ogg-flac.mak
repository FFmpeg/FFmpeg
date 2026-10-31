FATE_OGG_FLAC += fate-ogg-flac-chained-meta
fate-ogg-flac-chained-meta: REF = $(SRC_PATH)/tests/ref/fate/ogg-flac-chained-meta.txt
fate-ogg-flac-chained-meta: CMD = run $(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF) $(TARGET_SAMPLES)/ogg-flac/chained-meta.ogg

FATE_OGG_COPY_FLAC-$(call PARSERDEMDEC, FLAC, OGG, FLAC, OGG_MUXER) += fate-ogg-flac-copy-chained-meta
fate-ogg-flac-copy-chained-meta: $(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF)
fate-ogg-flac-copy-chained-meta: REF = $(SRC_PATH)/tests/ref/fate/ogg-flac-chained-meta.txt
fate-ogg-flac-copy-chained-meta: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel quiet -i $(TARGET_SAMPLES)/ogg-flac/chained-meta.ogg -c copy -f ogg -y" "$(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF)" ogg

# The pages of a bitexact remux, whose serial numbers the muxer chooses or
# takes from an explicit offset.
OGG_FLAC_SERIAL_MUX = "$(FFMPEG) -nostdin -hide_banner -loglevel quiet \
    -i $(TARGET_SAMPLES)/ogg-flac/chained-meta.ogg -c copy -fflags +bitexact"

FATE_OGG_COPY_FLAC-$(call PARSERDEMDEC, FLAC, OGG, FLAC, OGG_MUXER) += fate-ogg-flac-serial-default
fate-ogg-flac-serial-default: libavformat/tests/ogg_pages$(EXESUF)
fate-ogg-flac-serial-default: CMD = run_with_temp $(OGG_FLAC_SERIAL_MUX)" -f ogg -y" \
    "libavformat/tests/ogg_pages$(EXESUF)" ogg

FATE_OGG_COPY_FLAC-$(call PARSERDEMDEC, FLAC, OGG, FLAC, OGG_MUXER) += fate-ogg-flac-serial-offset
fate-ogg-flac-serial-offset: libavformat/tests/ogg_pages$(EXESUF)
fate-ogg-flac-serial-offset: CMD = run_with_temp $(OGG_FLAC_SERIAL_MUX)" -serial_offset 67574 -f ogg -y" \
    "libavformat/tests/ogg_pages$(EXESUF)" ogg

# Two links of tones piped as one chained stream, the second encoded with
# options $(2) that the first, encoded with $(1), does not share.
OGG_FLAC_CHAIN = { ffmpeg -auto_conversion_filters -f lavfi -i sine=frequency=440:duration=0.5 \
                     -c:a flac $(1) -flags +bitexact -fflags +bitexact -f ogg - && \
                   ffmpeg -auto_conversion_filters -f lavfi -i sine=frequency=660:duration=0.5 \
                     -c:a flac $(2) -flags +bitexact -fflags +bitexact -serial_offset 1 -f ogg -; } |

OGG_FLAC_CHAINED_BLOCKSIZE = $(call OGG_FLAC_CHAIN,-frame_size 1024,-frame_size 4096) framecrc -i -
OGG_FLAC_CHAINED_BPS = $(call OGG_FLAC_CHAIN,,-sample_fmt s32 -bits_per_raw_sample 24) \
    framecrc -auto_conversion_filters -i - -c:a pcm_s32le

# Each frame-threaded variant expects the output of its single-threaded test.
FATE_OGG_FLAC_CHAIN-$(call TRANSCODE, FLAC, OGG, LAVFI_INDEV SINE_FILTER FLAC_PARSER FD_PROTOCOL PCM_S16LE_DECODER) += fate-ogg-flac-chained-blocksize fate-ogg-flac-chained-blocksize-frame-threads
fate-ogg-flac-chained-blocksize: CMD = $(OGG_FLAC_CHAINED_BLOCKSIZE)
fate-ogg-flac-chained-blocksize-frame-threads: CMD = $(OGG_FLAC_CHAINED_BLOCKSIZE)
fate-ogg-flac-chained-blocksize-frame-threads: REF = $(SRC_PATH)/tests/ref/fate/ogg-flac-chained-blocksize
fate-ogg-flac-chained-blocksize-frame-threads: THREADS = 2
fate-ogg-flac-chained-blocksize-frame-threads: THREAD_TYPE = frame

FATE_OGG_FLAC_CHAIN-$(call TRANSCODE, FLAC, OGG, LAVFI_INDEV SINE_FILTER FLAC_PARSER ARESAMPLE_FILTER PCM_S32LE_ENCODER FD_PROTOCOL PCM_S16LE_DECODER) += fate-ogg-flac-chained-bps fate-ogg-flac-chained-bps-frame-threads
fate-ogg-flac-chained-bps: CMD = $(OGG_FLAC_CHAINED_BPS)
fate-ogg-flac-chained-bps-frame-threads: CMD = $(OGG_FLAC_CHAINED_BPS)
fate-ogg-flac-chained-bps-frame-threads: REF = $(SRC_PATH)/tests/ref/fate/ogg-flac-chained-bps
fate-ogg-flac-chained-bps-frame-threads: THREADS = 2
fate-ogg-flac-chained-bps-frame-threads: THREAD_TYPE = frame

FATE_OGG_FLAC-$(call DEMDEC, OGG, FLAC, FLAC_PARSER) += $(FATE_OGG_FLAC)

FATE_SAMPLES_DUMP_STREAM_META += $(FATE_OGG_FLAC-yes)

FATE_EXTERN += $(FATE_OGG_FLAC-yes)

FATE_SAMPLES_FFMPEG += $(FATE_OGG_COPY_FLAC-yes)

FATE_FFMPEG += $(FATE_OGG_FLAC_CHAIN-yes)

fate-ogg-flac: $(FATE_OGG_FLAC-yes) $(FATE_OGG_COPY_FLAC-yes) $(FATE_OGG_FLAC_CHAIN-yes)
