FATE_ALAC += fate-alac-16-level-0                                       \
             fate-alac-16-level-1                                       \
             fate-alac-16-level-2                                       \
             fate-alac-16-lpc-orders                                    \

fate-alac-16-level-%:    OPTS = -compression_level $(@:fate-alac-16-level-%=%)
fate-alac-16-lpc-orders: OPTS = -min_prediction_order 1 -max_prediction_order 30

fate-alac-16-%: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-alac-16-%: CMD = enc_dec_pcm mov wav s16le $(REF) -c alac $(OPTS)

fate-alac-%: CMP = oneoff
fate-alac-%: FUZZ = 0

FATE_SAMPLES_AVCONV-$(call ENCDEC, ALAC, MOV) += $(FATE_ALAC)
fate-alac: $(FATE_ALAC)
