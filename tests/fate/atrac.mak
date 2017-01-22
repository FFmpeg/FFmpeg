FATE_ATRAC1 += fate-atrac1-1
fate-atrac1-1: CMD = pcm -i $(TARGET_SAMPLES)/atrac1/test_tones_small.aea
fate-atrac1-1: REF = $(SAMPLES)/atrac1/test_tones_small_fixed_delay.pcm

FATE_ATRAC1 += fate-atrac1-2
fate-atrac1-2: CMD = pcm -i $(TARGET_SAMPLES)/atrac1/chirp_tone_10-16000.aea
fate-atrac1-2: REF = $(SAMPLES)/atrac1/chirp_tone_10-16000.pcm
fate-atrac1-2: FUZZ = 61

FATE_ATRAC1-$(call DEMDEC, AEA, ATRAC1) += $(FATE_ATRAC1)

FATE_ATRAC3 += fate-atrac3-1
fate-atrac3-1: CMD = pcm -i $(TARGET_SAMPLES)/atrac3/mc_sich_at3_066_small.wav
fate-atrac3-1: REF = $(SAMPLES)/atrac3/mc_sich_at3_066_small.pcm

FATE_ATRAC3 += fate-atrac3-2
fate-atrac3-2: CMD = pcm -i $(TARGET_SAMPLES)/atrac3/mc_sich_at3_105_small.wav
fate-atrac3-2: REF = $(SAMPLES)/atrac3/mc_sich_at3_105_small.pcm

FATE_ATRAC3 += fate-atrac3-3
fate-atrac3-3: CMD = pcm -i $(TARGET_SAMPLES)/atrac3/mc_sich_at3_132_small.wav
fate-atrac3-3: REF = $(SAMPLES)/atrac3/mc_sich_at3_132_small.pcm

FATE_ATRAC3-$(call DEMDEC, WAV, ATRAC3) += $(FATE_ATRAC3)

FATE_ATRAC3P += fate-atrac3p-1
fate-atrac3p-1: CMD = pcm -i $(TARGET_SAMPLES)/atrac3p/at3p_sample1.oma
fate-atrac3p-1: REF = $(SAMPLES)/atrac3p/at3p_sample1.pcm

FATE_ATRAC3P += fate-atrac3p-2
fate-atrac3p-2: CMD = pcm -i $(TARGET_SAMPLES)/atrac3p/sonateno14op27-2-cut.aa3
fate-atrac3p-2: REF = $(SAMPLES)/atrac3p/sonateno14op27-2-cut.pcm

FATE_ATRAC3P-$(call DEMDEC, OMA, ATRAC3P) += $(FATE_ATRAC3P)

FATE_ATRAC_ALL = $(FATE_ATRAC1-yes) $(FATE_ATRAC3-yes) $(FATE_ATRAC3P-yes)

$(FATE_ATRAC_ALL): CMP = oneoff

FATE_SAMPLES_AVCONV += $(FATE_ATRAC_ALL)

fate-atrac:   $(FATE_ATRAC_ALL)
fate-atrac3:  $(FATE_ATRAC3-yes)
fate-atrac3p: $(FATE_ATRAC3P-yes)
