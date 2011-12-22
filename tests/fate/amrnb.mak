FATE_AMRNB += fate-amrnb-4k75
fate-amrnb-4k75: CMD = pcm -i $(SAMPLES)/amrnb/4.75k.amr
fate-amrnb-4k75: CMP = stddev
fate-amrnb-4k75: REF = $(SAMPLES)/amrnb/4.75k.pcm
fate-amrnb-4k75: FUZZ = 1

FATE_AMRNB += fate-amrnb-5k15
fate-amrnb-5k15: CMD = pcm -i $(SAMPLES)/amrnb/5.15k.amr
fate-amrnb-5k15: CMP = stddev
fate-amrnb-5k15: REF = $(SAMPLES)/amrnb/5.15k.pcm
fate-amrnb-5k15: FUZZ = 1

FATE_AMRNB += fate-amrnb-5k9
fate-amrnb-5k9: CMD = pcm -i $(SAMPLES)/amrnb/5.9k.amr
fate-amrnb-5k9: CMP = stddev
fate-amrnb-5k9: REF = $(SAMPLES)/amrnb/5.9k.pcm
fate-amrnb-5k9: FUZZ = 1

FATE_AMRNB += fate-amrnb-6k7
fate-amrnb-6k7: CMD = pcm -i $(SAMPLES)/amrnb/6.7k.amr
fate-amrnb-6k7: CMP = stddev
fate-amrnb-6k7: REF = $(SAMPLES)/amrnb/6.7k.pcm
fate-amrnb-6k7: FUZZ = 1

FATE_AMRNB += fate-amrnb-7k4
fate-amrnb-7k4: CMD = pcm -i $(SAMPLES)/amrnb/7.4k.amr
fate-amrnb-7k4: CMP = stddev
fate-amrnb-7k4: REF = $(SAMPLES)/amrnb/7.4k.pcm
fate-amrnb-7k4: FUZZ = 1

FATE_AMRNB += fate-amrnb-7k95
fate-amrnb-7k95: CMD = pcm -i $(SAMPLES)/amrnb/7.95k.amr
fate-amrnb-7k95: CMP = stddev
fate-amrnb-7k95: REF = $(SAMPLES)/amrnb/7.95k.pcm
fate-amrnb-7k95: FUZZ = 1

FATE_AMRNB += fate-amrnb-10k2
fate-amrnb-10k2: CMD = pcm -i $(SAMPLES)/amrnb/10.2k.amr
fate-amrnb-10k2: CMP = stddev
fate-amrnb-10k2: REF = $(SAMPLES)/amrnb/10.2k.pcm
fate-amrnb-10k2: FUZZ = 1

FATE_AMRNB += fate-amrnb-12k2
fate-amrnb-12k2: CMD = pcm -i $(SAMPLES)/amrnb/12.2k.amr
fate-amrnb-12k2: CMP = stddev
fate-amrnb-12k2: REF = $(SAMPLES)/amrnb/12.2k.pcm
fate-amrnb-12k2: FUZZ = 1

FATE_TESTS += $(FATE_AMRNB)
fate-amrnb: $(FATE_AMRNB)
