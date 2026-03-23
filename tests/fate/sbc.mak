FATE_SBC_TRANSCODE-$(call ENCDEC2, SBC, PCM_S16LE, SBC, SBC_PARSER ARESAMPLE_FILTER WAV_DEMUXER WAV_MUXER) += \
    $(addprefix fate-sbc-, 16000-1 44100-1 48000-1 44100-2-joint-stereo 44100-2-stereo-low-delay 44100-2-stereo 44100-2-joint-stereo-four-subbands)
fate-sbc-16000-1: tests/data/asynth-16000-1.wav
fate-sbc-16000-1: CMD = enc_dec wav $(TARGET_PATH)/tests/data/asynth-16000-1.wav \
        sbc "-c:a sbc -profile msbc" wav
fate-sbc-16000-1: CMP_SHIFT=-146  # 73 samples

fate-sbc-48000-1: tests/data/asynth-48000-1.wav
fate-sbc-48000-1: CMD = enc_dec wav $(TARGET_PATH)/tests/data/asynth-48000-1.wav \
        sbc "-c:a sbc -sbc_delay 0.001 -b:a 500k" wav
fate-sbc-48000-1: CMP_SHIFT=-74 # 37 samples

fate-sbc-44100-1: tests/data/asynth-44100-1.wav
fate-sbc-44100-1: CMD = enc_dec wav $(TARGET_PATH)/tests/data/asynth-44100-1.wav \
        sbc "-c:a sbc -b:a 250k" wav
fate-sbc-44100-1: CMP_SHIFT=-146 # 73 samples

$(filter fate-sbc-44100-2%,$(FATE_SBC_TRANSCODE-yes)): tests/data/asynth-44100-2.wav
fate-sbc-44100-2-joint-stereo: CMD = enc_dec wav $(TARGET_PATH)/tests/data/asynth-44100-2.wav \
        sbc "-c:a sbc -b:a 50k" wav
fate-sbc-44100-2-joint-stereo: CMP_SHIFT=-292 # 73 samples

fate-sbc-44100-2-joint-stereo-four-subbands: CMD = enc_dec wav $(TARGET_PATH)/tests/data/asynth-44100-2.wav \
        sbc "-c:a sbc -b:a 450k" wav
fate-sbc-44100-2-joint-stereo-four-subbands: CMP_SHIFT=-148 # 37 samples

fate-sbc-44100-2-stereo-low-delay: CMD = enc_dec wav $(TARGET_PATH)/tests/data/asynth-44100-2.wav \
        sbc "-c:a sbc -b:a 200k -sbc_delay 0.003" wav
fate-sbc-44100-2-stereo-low-delay: CMP_SHIFT=-148 # 37 samples

fate-sbc-44100-2-stereo: CMD = enc_dec wav $(TARGET_PATH)/tests/data/asynth-44100-2.wav \
        sbc "-c:a sbc -b:a 200k" wav
fate-sbc-44100-2-stereo: CMP_SHIFT=-292 # 73 samples

FATE_SBC += $(FATE_SBC_TRANSCODE-yes)

FATE_FFMPEG += $(FATE_SBC)
fate-sbc: $(FATE_SBC)
