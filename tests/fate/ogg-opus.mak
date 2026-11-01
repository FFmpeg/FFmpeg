FATE_OGG_OPUS += fate-ogg-opus-chained-meta
fate-ogg-opus-chained-meta: REF = $(SRC_PATH)/tests/ref/fate/ogg-opus-chained-meta.txt
fate-ogg-opus-chained-meta: CMD = run $(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF) $(TARGET_SAMPLES)/ogg-opus/chained-meta.ogg

FATE_OGG_COPY_OPUS-$(call DEMDEC, OGG, OPUS, OGG_MUXER) += fate-ogg-opus-copy-chained-meta
fate-ogg-opus-copy-chained-meta: $(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF)
fate-ogg-opus-copy-chained-meta: REF = $(SRC_PATH)/tests/ref/fate/ogg-opus-chained-meta.txt
fate-ogg-opus-copy-chained-meta: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel quiet -i $(TARGET_SAMPLES)/ogg-opus/chained-meta.ogg -c copy -f ogg -y" "$(APITESTSDIR)/api-dump-stream-meta-test$(EXESUF)" ogg

# One link of a tone, given its frequency $(1), further muxer options $(2)
# and output file $(3).
OGG_OPUS_LINK = ffmpeg -auto_conversion_filters -f lavfi -i sine=frequency=$(1):duration=1 \
                  -c:a opus -strict experimental -flags +bitexact -fflags +bitexact $(2) -f ogg -y $(3)
OGG_OPUS_DECODE = ffmpeg -auto_conversion_filters -i $(1) -f s16le -

# Each link of a chain is a stream of its own, so decoding the links
# $(1).1.ogg and $(1).2.ogg chained must give what decoding them one by one
# does, written to $(1).pcm as the reference.
OGG_OPUS_CHAIN_DECODE = \
    { $(call OGG_OPUS_DECODE,$(1).1.ogg) && $(call OGG_OPUS_DECODE,$(1).2.ogg); } > $(1).pcm && \
    test -s $(1).pcm && \
    cat $(1).1.ogg $(1).2.ogg | $(call OGG_OPUS_DECODE,-)

# Two tones chained, $(2) holding further options for the second link.
# Links are written to files so that a failed encode fails the test.
OGG_OPUS_CHAINED = \
    $(call OGG_OPUS_LINK,440,,$(1).1.ogg) && \
    $(call OGG_OPUS_LINK,660,-serial_offset 1 $(2),$(1).2.ogg) && \
    $(call OGG_OPUS_CHAIN_DECODE,$(1))

OGG_OPUS_CHAINED_RESET_LINKS = tests/data/fate/ogg-opus-chained-reset.links
FATE_OGG_OPUS_CHAIN-$(call TRANSCODE, OPUS, OGG, LAVFI_INDEV SINE_FILTER ARESAMPLE_FILTER PCM_S16LE_DECODER PCM_S16LE_MUXER FD_PROTOCOL) += fate-ogg-opus-chained-reset
fate-ogg-opus-chained-reset: CMD = $(call OGG_OPUS_CHAINED,$(OGG_OPUS_CHAINED_RESET_LINKS),)
fate-ogg-opus-chained-reset: REF = $(OGG_OPUS_CHAINED_RESET_LINKS).pcm
fate-ogg-opus-chained-reset: CLEANFILES = $(addprefix $(OGG_OPUS_CHAINED_RESET_LINKS),.1.ogg .2.ogg .pcm)
fate-ogg-opus-chained-reset: CMP = stddev
fate-ogg-opus-chained-reset: CMP_TARGET = 0
fate-ogg-opus-chained-reset: FUZZ = 0

# The second link sets an output gain of -6 dB, which the first one does not.
OGG_OPUS_CHAINED_GAIN_LINKS = tests/data/fate/ogg-opus-chained-gain.links
FATE_OGG_OPUS_CHAIN-$(call TRANSCODE, OPUS, OGG, LAVFI_INDEV SINE_FILTER ARESAMPLE_FILTER PCM_S16LE_DECODER PCM_S16LE_MUXER FD_PROTOCOL OPUS_METADATA_BSF) += fate-ogg-opus-chained-gain
fate-ogg-opus-chained-gain: CMD = $(call OGG_OPUS_CHAINED,$(OGG_OPUS_CHAINED_GAIN_LINKS),-bsf:a opus_metadata=gain=-1536)
fate-ogg-opus-chained-gain: REF = $(OGG_OPUS_CHAINED_GAIN_LINKS).pcm
fate-ogg-opus-chained-gain: CLEANFILES = $(addprefix $(OGG_OPUS_CHAINED_GAIN_LINKS),.1.ogg .2.ogg .pcm)
fate-ogg-opus-chained-gain: CMP = stddev
fate-ogg-opus-chained-gain: CMP_TARGET = 0
fate-ogg-opus-chained-gain: FUZZ = 0

# Two SILK test vectors chained: the first link ends with samples still
# buffered in the decoder.
OGG_OPUS_REMUX = ffmpeg -i $(TARGET_SAMPLES)/opus/$(1).mka -c copy $(2) -f ogg -y $(3)
OGG_OPUS_CHAINED_SILK_LINKS = tests/data/fate/ogg-opus-chained-silk.links
FATE_OGG_OPUS_CHAIN_SAMPLES-$(call DEMMUX, MATROSKA, OGG, OGG_DEMUXER OPUS_DECODER ARESAMPLE_FILTER PCM_S16LE_ENCODER PCM_S16LE_MUXER FD_PROTOCOL PIPE_PROTOCOL) += fate-ogg-opus-chained-silk
fate-ogg-opus-chained-silk: CMD = \
    $(call OGG_OPUS_REMUX,testvector02,,$(OGG_OPUS_CHAINED_SILK_LINKS).1.ogg) && \
    $(call OGG_OPUS_REMUX,testvector03,-serial_offset 1,$(OGG_OPUS_CHAINED_SILK_LINKS).2.ogg) && \
    $(call OGG_OPUS_CHAIN_DECODE,$(OGG_OPUS_CHAINED_SILK_LINKS))
fate-ogg-opus-chained-silk: REF = $(OGG_OPUS_CHAINED_SILK_LINKS).pcm
fate-ogg-opus-chained-silk: CLEANFILES = $(addprefix $(OGG_OPUS_CHAINED_SILK_LINKS),.1.ogg .2.ogg .pcm)
fate-ogg-opus-chained-silk: CMP = stddev
fate-ogg-opus-chained-silk: CMP_TARGET = 0
fate-ogg-opus-chained-silk: FUZZ = 0

FATE_OGG_OPUS-$(call DEMDEC, OGG, OPUS) += $(FATE_OGG_OPUS)

FATE_SAMPLES_DUMP_STREAM_META += $(FATE_OGG_OPUS-yes)

FATE_EXTERN += $(FATE_OGG_OPUS-yes)

FATE_SAMPLES_FFMPEG += $(FATE_OGG_COPY_OPUS-yes) $(FATE_OGG_OPUS_CHAIN_SAMPLES-yes)

FATE_FFMPEG += $(FATE_OGG_OPUS_CHAIN-yes)

fate-ogg-opus: $(FATE_OGG_OPUS-yes) $(FATE_OGG_COPY_OPUS-yes) $(FATE_OGG_OPUS_CHAIN-yes) $(FATE_OGG_OPUS_CHAIN_SAMPLES-yes)
