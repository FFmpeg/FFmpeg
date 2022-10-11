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

FATE_FLAC-$(call ENCMUX, FLAC, FLAC) += $(FATE_FLAC)

FATE_SAMPLES_AVCONV += $(FATE_FLAC-yes)
fate-flac: $(FATE_FLAC)
