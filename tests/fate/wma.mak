FATE_WMAPRO-$(call DEMDEC, ASF, WMAPRO) += fate-wmapro-2ch
fate-wmapro-2ch: CMD = pcm -i $(TARGET_SAMPLES)/wmapro/Beethovens_9th-1_small.wma -frames 43
fate-wmapro-2ch: REF = $(SAMPLES)/wmapro/Beethovens_9th-1_small.pcm
fate-wmapro-2ch: SIZE_TOLERANCE = 8192

FATE_WMAPRO-$(call DEMDEC, ASF, WMAPRO) += fate-wmapro-5.1
fate-wmapro-5.1: CMD = pcm -i $(TARGET_SAMPLES)/wmapro/latin_192_mulitchannel_cut.wma -frames 101
fate-wmapro-5.1: REF = $(SAMPLES)/wmapro/latin_192_mulitchannel_cut.pcm
fate-wmapro-5.1: SIZE_TOLERANCE = 24576

FATE_WMAPRO-$(call DEMDEC, MOV, WMAPRO) += fate-wmapro-ism
fate-wmapro-ism: CMD = pcm -i $(TARGET_SAMPLES)/isom/vc1-wmapro.ism -vn
fate-wmapro-ism: REF = $(SAMPLES)/isom/vc1-wmapro.pcm
fate-wmapro-ism: SIZE_TOLERANCE = 4784

$(FATE_WMAPRO-yes): CMP = oneoff

FATE_SAMPLES_AVCONV += $(FATE_WMAPRO-yes)
fate-wmapro: $(FATE_WMAPRO-yes)

FATE_WMAVOICE-$(call DEMDEC, ASF, WMAVOICE) += fate-wmavoice-7k
fate-wmavoice-7k: CMD = pcm -i $(TARGET_SAMPLES)/wmavoice/streaming_CBR-7K.wma
fate-wmavoice-7k: REF = $(SAMPLES)/wmavoice/streaming_CBR-7K.pcm
fate-wmavoice-7k: FUZZ = 3

FATE_WMAVOICE-$(call DEMDEC, ASF, WMAVOICE) += fate-wmavoice-11k
fate-wmavoice-11k: CMD = pcm -i $(TARGET_SAMPLES)/wmavoice/streaming_CBR-11K.wma
fate-wmavoice-11k: REF = $(SAMPLES)/wmavoice/streaming_CBR-11K.pcm
fate-wmavoice-11k: FUZZ = 3

FATE_WMAVOICE-$(call DEMDEC, ASF, WMAVOICE) += fate-wmavoice-19k
fate-wmavoice-19k: CMD = pcm -i $(TARGET_SAMPLES)/wmavoice/streaming_CBR-19K.wma
fate-wmavoice-19k: REF = $(SAMPLES)/wmavoice/streaming_CBR-19K.pcm
fate-wmavoice-19k: FUZZ = 3

$(FATE_WMAVOICE-yes): CMP = stddev

FATE_SAMPLES_AVCONV += $(FATE_WMAVOICE-yes)
fate-wmavoice: $(FATE_WMAVOICE-yes)

FATE_WMA_ENCODE-$(call ENCDEC, WMAV1, ASF) += fate-wmav1-encode
fate-wmav1-encode: CMD = enc_dec_pcm asf wav s16le $(subst $(SAMPLES),$(TARGET_SAMPLES),$(REF)) -c:a wmav1 -b:a 128k
fate-wmav1-encode: CMP_SHIFT = 8192
fate-wmav1-encode: CMP_TARGET = 299.99
fate-wmav1-encode: SIZE_TOLERANCE = 4632

FATE_WMA_ENCODE-$(call ENCDEC, WMAV2, ASF) += fate-wmav2-encode
fate-wmav2-encode: CMD = enc_dec_pcm asf wav s16le $(subst $(SAMPLES),$(TARGET_SAMPLES),$(REF)) -c:a wmav2 -b:a 128k
fate-wmav2-encode: CMP_SHIFT = 8192
fate-wmav2-encode: CMP_TARGET = 267.92
fate-wmav2-encode: SIZE_TOLERANCE = 4632

$(FATE_WMA_ENCODE-yes): CMP = stddev
$(FATE_WMA_ENCODE-yes): REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav

FATE_SAMPLES_AVCONV += $(FATE_WMA_ENCODE-yes)
fate-wma-encode: $(FATE_WMA_ENCODE-yes)
