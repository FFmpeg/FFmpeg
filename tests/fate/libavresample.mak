CROSS_TEST = $(foreach I,$(1),                                          \
                 $(foreach J,$(1),                                      \
                     $(if $(filter-out $(I),$(J)),                      \
                         $(eval $(call $(2),$(I),$(J),$(3),$(4),$(5))), \
                     )))

MIX_CHANNELS = 1 2 3 4 5 6 7 8

define MIX
FATE_LAVR_MIX += fate-lavr-mix-$(3)-$(1)-$(2)
fate-lavr-mix-$(3)-$(1)-$(2): tests/data/asynth-44100-$(1).wav
fate-lavr-mix-$(3)-$(1)-$(2): CMD = ffmpeg -i $(TARGET_PATH)/tests/data/asynth-44100-$(1).wav -ac $(2) -mix_coeff_type $(3) -internal_sample_fmt $(4) -f s16le -af atrim=end_sample=1024 -
fate-lavr-mix-$(3)-$(1)-$(2): CMP = oneoff
fate-lavr-mix-$(3)-$(1)-$(2): REF = $(SAMPLES)/lavr/lavr-mix-$(3)-$(1)-$(2)
endef

$(call CROSS_TEST,$(MIX_CHANNELS),MIX,q8,s16p)
$(call CROSS_TEST,$(MIX_CHANNELS),MIX,q15,s16p)
$(call CROSS_TEST,$(MIX_CHANNELS),MIX,flt,fltp)

# test output zeroing with skipped corresponding input
FATE_LAVR_MIX-$(call FILTERDEMDECENCMUX, CHANNELMAP RESAMPLE, WAV, PCM_S16LE, PCM_S16LE, WAV) += fate-lavr-mix-output-zero
fate-lavr-mix-output-zero: tests/data/filtergraphs/lavr_mix_output_zero tests/data/asynth-44100-4.wav
fate-lavr-mix-output-zero: CMP = oneoff
fate-lavr-mix-output-zero: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/asynth-44100-4.wav -filter_script $(TARGET_PATH)/tests/data/filtergraphs/lavr_mix_output_zero -f s16le -
fate-lavr-mix-output-zero: REF = $(SAMPLES)/lavr/lavr-mix-output-zero

FATE_LAVR_MIX-$(call FILTERDEMDECENCMUX, RESAMPLE, WAV, PCM_S16LE, PCM_S16LE, WAV) += $(FATE_LAVR_MIX)
fate-lavr-mix: $(FATE_LAVR_MIX-yes)
#FATE_LAVR += $(FATE_LAVR_MIX-yes)

SAMPLERATES = 2626 8000 44100 48000 96000

define RESAMPLE
FATE_LAVR_RESAMPLE += fate-lavr-resample-$(3)-$(1)-$(2)
fate-lavr-resample-$(3)-$(1)-$(2): tests/data/asynth-$(1)-1.wav
fate-lavr-resample-$(3)-$(1)-$(2): CMD = ffmpeg -i $(TARGET_PATH)/tests/data/asynth-$(1)-1.wav -ar $(2) -internal_sample_fmt $(3) -f $(4) -af atrim=end_sample=10240 -
fate-lavr-resample-$(3)-$(1)-$(2): CMP = oneoff
fate-lavr-resample-$(3)-$(1)-$(2): CMP_UNIT = $(5)
fate-lavr-resample-$(3)-$(1)-$(2): FUZZ = 6
fate-lavr-resample-$(3)-$(1)-$(2): REF = $(SAMPLES)/lavr/lavr-resample-$(3)-$(1)-$(2)-v3
endef

$(call CROSS_TEST,$(SAMPLERATES),RESAMPLE,s16p,s16le,s16)
$(call CROSS_TEST,$(SAMPLERATES),RESAMPLE,s32p,s32le,s16)
$(call CROSS_TEST,$(SAMPLERATES),RESAMPLE,fltp,f32le,f32)
$(call CROSS_TEST,$(SAMPLERATES),RESAMPLE,dblp,f64le,f64)

FATE_LAVR_RESAMPLE += fate-lavr-resample-linear
fate-lavr-resample-linear: tests/data/asynth-44100-1.wav
fate-lavr-resample-linear: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/asynth-44100-1.wav -ar 48000 -filter_size 32 -linear_interp 1 -f s16le -af atrim=end_sample=10240 -
fate-lavr-resample-linear: CMP = oneoff
fate-lavr-resample-linear: CMP_UNIT = s16
fate-lavr-resample-linear: REF = $(SAMPLES)/lavr/lavr-resample-linear

FATE_LAVR_RESAMPLE += fate-lavr-resample-nearest
fate-lavr-resample-nearest: tests/data/asynth-48000-1.wav
fate-lavr-resample-nearest: CMD = ffmpeg -i $(TARGET_PATH)/tests/data/asynth-48000-1.wav -ar 44100 -filter_size 0 -phase_shift 0 -f s16le -af atrim=end_sample=10240 -
fate-lavr-resample-nearest: CMP = oneoff
fate-lavr-resample-nearest: CMP_UNIT = s16
fate-lavr-resample-nearest: REF = $(SAMPLES)/lavr/lavr-resample-nearest

FATE_LAVR_RESAMPLE-$(call FILTERDEMDECENCMUX, RESAMPLE, WAV, PCM_S16LE, PCM_S16LE, WAV) += $(FATE_LAVR_RESAMPLE)
fate-lavr-resample: $(FATE_LAVR_RESAMPLE-yes)
#FATE_LAVR += $(FATE_LAVR_RESAMPLE-yes)

FATE_SAMPLES_AVCONV += $(FATE_LAVR)
fate-lavr: $(FATE_LAVR)
