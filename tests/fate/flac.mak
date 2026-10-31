FATE_FLAC += fate-flac-16-chmode-indep                                  \
             fate-flac-16-chmode-left_side                              \
             fate-flac-16-chmode-mid_side                               \
             fate-flac-16-chmode-right_side                             \
             fate-flac-16-fixed                                         \
             fate-flac-16-lpc-cholesky                                  \
             fate-flac-16-lpc-levinson                                  \
             fate-flac-24-comp-8                                        \
             fate-flac-32-wasted-bits                                   \
             fate-flac-rice-params                                      \

fate-flac-16-chmode-%: OPTS = -ch_mode $(@:fate-flac-16-chmode-%=%)
fate-flac-16-fixed:    OPTS = -lpc_type fixed
fate-flac-16-lpc-%:    OPTS = -lpc_type $(@:fate-flac-16-lpc-%=%)

fate-flac-16-%: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-flac-16-%: CMD = enc_dec_pcm flac wav s16le $(subst $(SAMPLES),$(TARGET_SAMPLES),$(REF)) -c flac $(OPTS)

fate-flac-24-comp-%: OPTS = -compression_level $(@:fate-flac-24-comp-%=%)

fate-flac-24-%: REF = $(SAMPLES)/audio-reference/divertimenti_2ch_96kHz_s24.wav
fate-flac-24-%: CMD = enc_dec_pcm flac wav s24le $(subst $(SAMPLES),$(TARGET_SAMPLES),$(REF)) -c flac $(OPTS)

fate-flac-32-wasted-bits: REF = $(SAMPLES)/audio-reference/drums_2ch_44kHz_s32_wastedbits.wav
fate-flac-32-wasted-bits: CMD = enc_dec_pcm flac wav s32le $(subst $(SAMPLES),$(TARGET_SAMPLES),$(REF)) -c flac -strict experimental

fate-flac-rice-params: REF = $(SAMPLES)/audio-reference/chorusnoise_2ch_44kHz_s16.wav
fate-flac-rice-params: CMD = enc_dec_pcm flac wav s16le $(subst $(SAMPLES),$(TARGET_SAMPLES),$(REF)) -c flac

fate-flac-%: CMP = oneoff
fate-flac-%: FUZZ = 0

FATE_FLAC-$(call ENCMUX, FLAC, FLAC, PIPE_PROTOCOL) += $(FATE_FLAC)

# One stream whose second half changes the bit depth, or exceeds the block size
# its STREAMINFO declares, without a new STREAMINFO: the decoder rejects those
# frames.
#
# The first half ends on a whole frame, since a short frame anywhere but last
# gets a bogus timestamp from the parser.
FLAC_MIDSTREAM_CHANGE = { ffmpeg -auto_conversion_filters -f lavfi -i sine=frequency=440:duration=0.5 \
                            -af atrim=end_sample=21504 \
                            -c:a flac -frame_size 1024 -flags +bitexact -fflags +bitexact -f flac - && \
                          ffmpeg -auto_conversion_filters -f lavfi -i sine=frequency=660:duration=0.5 \
                            -c:a flac $(1) -flags +bitexact -fflags +bitexact -f flac -write_header 0 -; } |

FLAC_MIDSTREAM_BLOCKSIZE = $(call FLAC_MIDSTREAM_CHANGE,-frame_size 4096) framecrc -i -
FLAC_MIDSTREAM_BPS = $(call FLAC_MIDSTREAM_CHANGE,-sample_fmt s32 -bits_per_raw_sample 24) \
    framecrc -auto_conversion_filters -i - -c:a pcm_s32le

# Each frame-threaded variant expects the output of its single-threaded test.
FATE_FLAC_MIDSTREAM-$(call TRANSCODE, FLAC, FLAC, LAVFI_INDEV SINE_FILTER ATRIM_FILTER FLAC_PARSER FD_PROTOCOL PCM_S16LE_DECODER) += fate-flac-midstream-blocksize fate-flac-midstream-blocksize-frame-threads
fate-flac-midstream-blocksize: CMD = $(FLAC_MIDSTREAM_BLOCKSIZE)
fate-flac-midstream-blocksize-frame-threads: CMD = $(FLAC_MIDSTREAM_BLOCKSIZE)
fate-flac-midstream-blocksize-frame-threads: REF = $(SRC_PATH)/tests/ref/fate/flac-midstream-blocksize
fate-flac-midstream-blocksize-frame-threads: THREADS = 2
fate-flac-midstream-blocksize-frame-threads: THREAD_TYPE = frame

FATE_FLAC_MIDSTREAM-$(call TRANSCODE, FLAC, FLAC, LAVFI_INDEV SINE_FILTER ATRIM_FILTER FLAC_PARSER ARESAMPLE_FILTER PCM_S32LE_ENCODER FD_PROTOCOL PCM_S16LE_DECODER) += fate-flac-midstream-bps fate-flac-midstream-bps-frame-threads
fate-flac-midstream-bps: CMD = $(FLAC_MIDSTREAM_BPS)
fate-flac-midstream-bps-frame-threads: CMD = $(FLAC_MIDSTREAM_BPS)
fate-flac-midstream-bps-frame-threads: REF = $(SRC_PATH)/tests/ref/fate/flac-midstream-bps
fate-flac-midstream-bps-frame-threads: THREADS = 2
fate-flac-midstream-bps-frame-threads: THREAD_TYPE = frame

$(FATE_FLAC_MIDSTREAM-yes): CMP = diff

FATE_FFMPEG += $(FATE_FLAC_MIDSTREAM-yes)

FATE_SAMPLES_AVCONV += $(FATE_FLAC-yes)
fate-flac: $(FATE_FLAC) $(FATE_FLAC_MIDSTREAM-yes)
