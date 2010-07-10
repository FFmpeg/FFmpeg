FATE2_TESTS += fate-twinvq
fate-twinvq: CMD = $(TARGET_PATH)/ffmpeg -i $(SAMPLES)/vqf/achterba.vqf -f s16le -
fate-twinvq: CMP = oneoff
fate-twinvq: REF = $(SAMPLES)/vqf/achterba.pcm

FATE2_TESTS += fate-sipr-16k
fate-sipr-16k: CMD = $(TARGET_PATH)/ffmpeg -i $(SAMPLES)/sipr/sipr_16k.rm -f s16le -
fate-sipr-16k: CMP = oneoff
fate-sipr-16k: REF = $(SAMPLES)/sipr/sipr_16k.pcm

FATE2_TESTS += fate-sipr-8k5
fate-sipr-8k5: CMD = $(TARGET_PATH)/ffmpeg -i $(SAMPLES)/sipr/sipr_8k5.rm -f s16le -
fate-sipr-8k5: CMP = oneoff
fate-sipr-8k5: REF = $(SAMPLES)/sipr/sipr_8k5.pcm

FATE2_TESTS += fate-sipr-6k5
fate-sipr-6k5: CMD = $(TARGET_PATH)/ffmpeg -i $(SAMPLES)/sipr/sipr_6k5.rm -f s16le -
fate-sipr-6k5: CMP = oneoff
fate-sipr-6k5: REF = $(SAMPLES)/sipr/sipr_6k5.pcm

FATE2_TESTS += fate-sipr-5k0
fate-sipr-5k0: CMD = $(TARGET_PATH)/ffmpeg -i $(SAMPLES)/sipr/sipr_5k0.rm -f s16le -
fate-sipr-5k0: CMP = oneoff
fate-sipr-5k0: REF = $(SAMPLES)/sipr/sipr_5k0.pcm

FATE2_TESTS += fate-aac-al04_44
fate-aac-al04_44: CMD = $(TARGET_PATH)/ffmpeg -i $(SAMPLES)/aac/al04_44.mp4 -f s16le -
fate-aac-al04_44: CMP = oneoff
fate-aac-al04_44: REF = $(SAMPLES)/aac/al04_44.s16
fate-aac-al04_44: FUZZ = 2

FATE2_TESTS += fate-aac-al07_96
fate-aac-al07_96: CMD = $(TARGET_PATH)/ffmpeg -i $(SAMPLES)/aac/al07_96.mp4 -f s16le -
fate-aac-al07_96: CMP = oneoff
fate-aac-al07_96: REF = $(SAMPLES)/aac/al07_96.s16
fate-aac-al07_96: FUZZ = 2

FATE2_TESTS += fate-aac-am00_88
fate-aac-am00_88: CMD = $(TARGET_PATH)/ffmpeg -i $(SAMPLES)/aac/am00_88.mp4 -f s16le -
fate-aac-am00_88: CMP = oneoff
fate-aac-am00_88: REF = $(SAMPLES)/aac/am00_88.s16
fate-aac-am00_88: FUZZ = 2

FATE2_TESTS += fate-aac-al_sbr_hq_cm_48_2
fate-aac-al_sbr_hq_cm_48_2: CMD = $(TARGET_PATH)/ffmpeg -i $(SAMPLES)/aac/al_sbr_cm_48_2.mp4 -f s16le -
fate-aac-al_sbr_hq_cm_48_2: CMP = oneoff
fate-aac-al_sbr_hq_cm_48_2: REF = $(SAMPLES)/aac/al_sbr_hq_cm_48_2.s16
fate-aac-al_sbr_hq_cm_48_2: FUZZ = 2

FATE2_TESTS += fate-aac-al_sbr_ps_06_ur
fate-aac-al_sbr_ps_06_ur: CMD = $(TARGET_PATH)/ffmpeg -i $(SAMPLES)/aac/al_sbr_ps_06_new.mp4 -f s16le -
fate-aac-al_sbr_ps_06_ur: CMP = oneoff
fate-aac-al_sbr_ps_06_ur: REF = $(SAMPLES)/aac/al_sbr_ps_06_ur.s16
fate-aac-al_sbr_ps_06_ur: FUZZ = 2
