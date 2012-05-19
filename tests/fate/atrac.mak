FATE_ATRAC += fate-atrac1
fate-atrac1: CMD = pcm -i $(SAMPLES)/atrac1/test_tones_small.aea
fate-atrac1: CMP = oneoff
fate-atrac1: REF = $(SAMPLES)/atrac1/test_tones_small.pcm

FATE_ATRAC += fate-atrac3-1
fate-atrac3-1: CMD = pcm -i $(SAMPLES)/atrac3/mc_sich_at3_066_small.wav
fate-atrac3-1: CMP = oneoff
fate-atrac3-1: REF = $(SAMPLES)/atrac3/mc_sich_at3_066_small.pcm

FATE_ATRAC += fate-atrac3-2
fate-atrac3-2: CMD = pcm -i $(SAMPLES)/atrac3/mc_sich_at3_105_small.wav
fate-atrac3-2: CMP = oneoff
fate-atrac3-2: REF = $(SAMPLES)/atrac3/mc_sich_at3_105_small.pcm

FATE_ATRAC += fate-atrac3-3
fate-atrac3-3: CMD = pcm -i $(SAMPLES)/atrac3/mc_sich_at3_132_small.wav
fate-atrac3-3: CMP = oneoff
fate-atrac3-3: REF = $(SAMPLES)/atrac3/mc_sich_at3_132_small.pcm

FATE_SAMPLES_AVCONV += $(FATE_ATRAC)
fate-atrac: $(FATE_ATRAC)
