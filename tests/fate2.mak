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
