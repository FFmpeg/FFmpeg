FATE_FLAC += fate-flac-chmode-indep                                     \
             fate-flac-chmode-left_side                                 \
             fate-flac-chmode-mid_side                                  \
             fate-flac-chmode-right_side                                \
             fate-flac-fixed                                            \
             fate-flac-lpc-cholesky                                     \
             fate-flac-lpc-levinson                                     \

fate-flac-chmode-%: OPTS = -ch_mode $(@:fate-flac-chmode-%=%)
fate-flac-fixed: OPTS = -lpc_type fixed
fate-flac-lpc-%: OPTS = -lpc_type $(@:fate-flac-lpc-%=%)

fate-flac-%: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-flac-%: CMD = enc_dec_pcm flac wav s16le $(REF) -c flac $(OPTS)
fate-flac-%: CMP = oneoff
fate-flac-%: FUZZ = 0

FATE_SAMPLES_AVCONV += $(FATE_FLAC)
fate-flac: $(FATE_FLAC)
