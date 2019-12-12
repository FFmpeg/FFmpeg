FATE_AMRWB += fate-amrwb-6k60
fate-amrwb-6k60: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-6k60.awb
fate-amrwb-6k60: REF = $(SAMPLES)/amrwb/seed-6k60.pcm

FATE_AMRWB += fate-amrwb-8k85
fate-amrwb-8k85: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-8k85.awb
fate-amrwb-8k85: REF = $(SAMPLES)/amrwb/seed-8k85.pcm

FATE_AMRWB += fate-amrwb-12k65
fate-amrwb-12k65: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-12k65.awb
fate-amrwb-12k65: REF = $(SAMPLES)/amrwb/seed-12k65.pcm

FATE_AMRWB += fate-amrwb-14k25
fate-amrwb-14k25: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-14k25.awb
fate-amrwb-14k25: REF = $(SAMPLES)/amrwb/seed-14k25.pcm
fate-amrwb-14k25: FUZZ = 2.6

FATE_AMRWB += fate-amrwb-15k85
fate-amrwb-15k85: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-15k85.awb
fate-amrwb-15k85: REF = $(SAMPLES)/amrwb/seed-15k85.pcm

FATE_AMRWB += fate-amrwb-18k25
fate-amrwb-18k25: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-18k25.awb
fate-amrwb-18k25: REF = $(SAMPLES)/amrwb/seed-18k25.pcm

FATE_AMRWB += fate-amrwb-19k85
fate-amrwb-19k85: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-19k85.awb
fate-amrwb-19k85: REF = $(SAMPLES)/amrwb/seed-19k85.pcm

FATE_AMRWB += fate-amrwb-23k05
fate-amrwb-23k05: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-23k05.awb
fate-amrwb-23k05: REF = $(SAMPLES)/amrwb/seed-23k05.pcm
fate-amrwb-23k05: FUZZ = 2

FATE_AMRWB += fate-amrwb-23k85
fate-amrwb-23k85: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/seed-23k85.awb
fate-amrwb-23k85: REF = $(SAMPLES)/amrwb/seed-23k85.pcm
fate-amrwb-23k85: FUZZ = 2

FATE_AMRWB += fate-amrwb-23k85-2
fate-amrwb-23k85-2: CMD = pcm -i $(TARGET_SAMPLES)/amrwb/deus-23k85.awb
fate-amrwb-23k85-2: REF = $(SAMPLES)/amrwb/deus-23k85.pcm

$(FATE_AMRWB): CMP = stddev

FATE_SAMPLES_AVCONV-$(call DEMDEC, AMR, AMRWB) += $(FATE_AMRWB)
fate-amrwb: $(FATE_AMRWB)
