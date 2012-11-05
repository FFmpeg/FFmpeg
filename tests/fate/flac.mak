FATE_FLAC += fate-flac-16-chmode-indep                                  \
             fate-flac-16-chmode-left_side                              \
             fate-flac-16-chmode-mid_side                               \
             fate-flac-16-chmode-right_side                             \
             fate-flac-16-fixed                                         \
             fate-flac-16-lpc-cholesky                                  \
             fate-flac-16-lpc-levinson                                  \
             fate-flac-24-comp-8                                        \

fate-flac-16-chmode-%: OPTS = -ch_mode $(@:fate-flac-16-chmode-%=%)
fate-flac-16-fixed:    OPTS = -lpc_type fixed
fate-flac-16-lpc-%:    OPTS = -lpc_type $(@:fate-flac-16-lpc-%=%)

fate-flac-16-%: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-flac-16-%: CMD = enc_dec_pcm flac wav s16le $(REF) -c flac $(OPTS)

fate-flac-24-comp-%: OPTS = -compression_level $(@:fate-flac-24-comp-%=%)

fate-flac-24-%: REF = $(SAMPLES)/audio-reference/divertimenti_2ch_96kHz_s24.wav
fate-flac-24-%: CMD = enc_dec_pcm flac wav s24le $(REF) -c flac $(OPTS)

fate-flac-%: CMP = oneoff
fate-flac-%: FUZZ = 0

FATE_SAMPLES_AVCONV += $(FATE_FLAC)
fate-flac: $(FATE_FLAC)
