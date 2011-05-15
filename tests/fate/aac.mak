FATE_AAC += fate-aac-al04_44
fate-aac-al04_44: CMD = pcm -i $(SAMPLES)/aac/al04_44.mp4
fate-aac-al04_44: REF = $(SAMPLES)/aac/al04_44.s16

FATE_AAC += fate-aac-al07_96
fate-aac-al07_96: CMD = pcm -i $(SAMPLES)/aac/al07_96.mp4
fate-aac-al07_96: REF = $(SAMPLES)/aac/al07_96.s16

FATE_AAC += fate-aac-am00_88
fate-aac-am00_88: CMD = pcm -i $(SAMPLES)/aac/am00_88.mp4
fate-aac-am00_88: REF = $(SAMPLES)/aac/am00_88.s16

FATE_AAC += fate-aac-al_sbr_hq_cm_48_2
fate-aac-al_sbr_hq_cm_48_2: CMD = pcm -i $(SAMPLES)/aac/al_sbr_cm_48_2.mp4
fate-aac-al_sbr_hq_cm_48_2: REF = $(SAMPLES)/aac/al_sbr_hq_cm_48_2.s16

FATE_AAC += fate-aac-al_sbr_ps_06_ur
fate-aac-al_sbr_ps_06_ur: CMD = pcm -i $(SAMPLES)/aac/al_sbr_ps_06_new.mp4
fate-aac-al_sbr_ps_06_ur: REF = $(SAMPLES)/aac/al_sbr_ps_06_ur.s16

FATE_AAC += fate-aac-latm_000000001180bc60
fate-aac-latm_000000001180bc60: CMD = pcm -i $(SAMPLES)/aac/latm_000000001180bc60.mpg
fate-aac-latm_000000001180bc60: REF = $(SAMPLES)/aac/latm_000000001180bc60.s16

FATE_AAC += fate-aac-ap05_48
fate-aac-ap05_48: CMD = pcm -i $(SAMPLES)/aac/ap05_48.mp4
fate-aac-ap05_48: REF = $(SAMPLES)/aac/ap05_48.s16

FATE_TESTS += $(FATE_AAC)
fate-aac: $(FATE_AAC)
$(FATE_AAC): CMP = oneoff
$(FATE_AAC): FUZZ = 2
