FATE_ATRAC1-$(call DEMDEC, AEA, ATRAC1) += fate-atrac1
fate-atrac1: CMD = pcm -i $(SAMPLES)/atrac1/test_tones_small.aea
fate-atrac1: REF = $(SAMPLES)/atrac1/test_tones_small.pcm

FATE_ATRAC3 += fate-atrac3-1
fate-atrac3-1: CMD = pcm -i $(SAMPLES)/atrac3/mc_sich_at3_066_small.wav
fate-atrac3-1: REF = $(SAMPLES)/atrac3/mc_sich_at3_066_small.pcm

FATE_ATRAC3 += fate-atrac3-2
fate-atrac3-2: CMD = pcm -i $(SAMPLES)/atrac3/mc_sich_at3_105_small.wav
fate-atrac3-2: REF = $(SAMPLES)/atrac3/mc_sich_at3_105_small.pcm

FATE_ATRAC3 += fate-atrac3-3
fate-atrac3-3: CMD = pcm -i $(SAMPLES)/atrac3/mc_sich_at3_132_small.wav
fate-atrac3-3: REF = $(SAMPLES)/atrac3/mc_sich_at3_132_small.pcm

FATE_ATRAC3-$(call DEMDEC, WAV, ATRAC3) += $(FATE_ATRAC3)

FATE_ATRAC_ALL = $(FATE_ATRAC1-yes) $(FATE_ATRAC3-yes)

$(FATE_ATRAC_ALL): CMP = oneoff

FATE_SAMPLES_AVCONV += $(FATE_ATRAC_ALL)

fate-atrac:  $(FATE_ATRAC_ALL)
fate-atrac3: $(FATE_ATRAC3-yes)
