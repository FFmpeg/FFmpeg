FATE_ALAC += fate-alac-level-0                                          \
             fate-alac-level-1                                          \
             fate-alac-level-2                                          \
             fate-alac-lpc-orders                                       \

fate-alac-level-%: OPTS = -compression_level $(@:fate-alac-level-%=%)
fate-alac-lpc-orders: OPTS = -min_prediction_order 1 -max_prediction_order 30

fate-alac-%: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-alac-%: CMD = enc_dec_pcm mov wav s16le $(REF) -c alac $(OPTS)
fate-alac-%: CMP = oneoff
fate-alac-%: FUZZ = 0

FATE_SAMPLES_AVCONV += $(FATE_ALAC)
fate-alac: $(FATE_ALAC)
