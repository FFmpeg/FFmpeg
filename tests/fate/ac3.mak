FATE_AC3 += fate-ac3-2.0
fate-ac3-2.0: CMD = pcm -i $(TARGET_SAMPLES)/ac3/monsters_inc_2.0_192_small.ac3
fate-ac3-2.0: REF = $(SAMPLES)/ac3/monsters_inc_2.0_192_small_v2.pcm

FATE_AC3 += fate-ac3-4.0
fate-ac3-4.0: CMD = pcm -i $(TARGET_SAMPLES)/ac3/millers_crossing_4.0.ac3
fate-ac3-4.0: REF = $(SAMPLES)/ac3/millers_crossing_4.0_v2.pcm

#downmix 4.0 -> front channel
FATE_AC3 += fate-ac3-4.0-downmix-mono
fate-ac3-4.0-downmix-mono: CMD = pcm -downmix mono -i $(TARGET_SAMPLES)/ac3/millers_crossing_4.0.ac3
fate-ac3-4.0-downmix-mono: REF = $(SAMPLES)/ac3/millers_crossing_4.0_mono_v2.pcm

FATE_AC3 += fate-ac3-4.0-downmix-stereo
fate-ac3-4.0-downmix-stereo: CMD = pcm -downmix stereo -i $(TARGET_SAMPLES)/ac3/millers_crossing_4.0.ac3
fate-ac3-4.0-downmix-stereo: REF = $(SAMPLES)/ac3/millers_crossing_4.0_stereo_v2.pcm

FATE_AC3 += fate-ac3-5.1
fate-ac3-5.1: CMD = pcm -i $(TARGET_SAMPLES)/ac3/monsters_inc_5.1_448_small.ac3
fate-ac3-5.1: REF = $(SAMPLES)/ac3/monsters_inc_5.1_448_small_v2.pcm

FATE_AC3 += fate-ac3-5.1-downmix-mono
fate-ac3-5.1-downmix-mono: CMD = pcm -downmix FC -i $(TARGET_SAMPLES)/ac3/monsters_inc_5.1_448_small.ac3
fate-ac3-5.1-downmix-mono: REF = $(SAMPLES)/ac3/monsters_inc_5.1_448_small_mono_v2.pcm

FATE_AC3 += fate-ac3-5.1-downmix-stereo
fate-ac3-5.1-downmix-stereo: CMD = pcm -downmix stereo -i $(TARGET_SAMPLES)/ac3/monsters_inc_5.1_448_small.ac3
fate-ac3-5.1-downmix-stereo: REF = $(SAMPLES)/ac3/monsters_inc_5.1_448_small_stereo_v2.pcm

FATE_AC3 += fate-ac3-fixed-2.0
fate-ac3-fixed-2.0: CMD = pcm -c ac3_fixed -i $(TARGET_SAMPLES)/ac3/monsters_inc_2.0_192_small.ac3
fate-ac3-fixed-2.0: REF = $(SAMPLES)/ac3/monsters_inc_2.0_192_small_v2.pcm

FATE_AC3 += fate-ac3-fixed-4.0-downmix-mono
fate-ac3-fixed-4.0-downmix-mono: CMD = pcm -c ac3_fixed -downmix mono -i $(TARGET_SAMPLES)/ac3/millers_crossing_4.0.ac3
fate-ac3-fixed-4.0-downmix-mono: REF = $(SAMPLES)/ac3/millers_crossing_4.0_mono_v2.pcm

FATE_AC3 += fate-ac3-fixed-5.1-downmix-mono
fate-ac3-fixed-5.1-downmix-mono: CMD = pcm -c ac3_fixed -downmix mono -i $(TARGET_SAMPLES)/ac3/monsters_inc_5.1_448_small.ac3
fate-ac3-fixed-5.1-downmix-mono: REF = $(SAMPLES)/ac3/monsters_inc_5.1_448_small_mono_v2.pcm

FATE_AC3 += fate-ac3-fixed-5.1-downmix-stereo
fate-ac3-fixed-5.1-downmix-stereo: CMD = pcm -c ac3_fixed -downmix stereo -i $(TARGET_SAMPLES)/ac3/monsters_inc_5.1_448_small.ac3
fate-ac3-fixed-5.1-downmix-stereo: REF = $(SAMPLES)/ac3/monsters_inc_5.1_448_small_stereo_v2.pcm

FATE_EAC3 += fate-eac3-1
fate-eac3-1: CMD = pcm -i $(TARGET_SAMPLES)/eac3/csi_miami_5.1_256_spx_small.eac3
fate-eac3-1: REF = $(SAMPLES)/eac3/csi_miami_5.1_256_spx_small_v2.pcm

FATE_EAC3 += fate-eac3-2
fate-eac3-2: CMD = pcm -i $(TARGET_SAMPLES)/eac3/csi_miami_stereo_128_spx_small.eac3
fate-eac3-2: REF = $(SAMPLES)/eac3/csi_miami_stereo_128_spx_small_v2.pcm

FATE_EAC3 += fate-eac3-3
fate-eac3-3: CMD = pcm -i $(TARGET_SAMPLES)/eac3/matrix2_commentary1_stereo_192_small.eac3
fate-eac3-3: REF = $(SAMPLES)/eac3/matrix2_commentary1_stereo_192_small_v2.pcm

FATE_EAC3 += fate-eac3-4
fate-eac3-4: CMD = pcm -i $(TARGET_SAMPLES)/eac3/serenity_english_5.1_1536_small.eac3
fate-eac3-4: REF = $(SAMPLES)/eac3/serenity_english_5.1_1536_small_v2.pcm

FATE_EAC3 += fate-eac3-5
fate-eac3-5: CMD = pcm -i $(TARGET_SAMPLES)/eac3/the_great_wall_7.1.eac3
fate-eac3-5: REF = $(SAMPLES)/eac3/the_great_wall_7.1.pcm

$(FATE_AC3) $(FATE_EAC3): CMP = oneoff

FATE_AC3-$(call  DEMDEC, AC3,  AC3,  ARESAMPLE_FILTER)  += $(FATE_AC3)
FATE_EAC3-$(call DEMDEC, EAC3, EAC3, ARESAMPLE_FILTER) += $(FATE_EAC3)

FATE_AC3-$(call ENCDEC, AC3, AC3, ARESAMPLE_FILTER) += fate-ac3-encode
fate-ac3-encode: CMD = enc_dec_pcm ac3 wav s16le $(subst $(SAMPLES),$(TARGET_SAMPLES),$(REF)) -c:a ac3 -b:a 128k
fate-ac3-encode: CMP_SHIFT = -1024
fate-ac3-encode: CMP_TARGET = 404.53
fate-ac3-encode: SIZE_TOLERANCE = 488


FATE_EAC3-$(call ENCDEC, EAC3, EAC3, ARESAMPLE_FILTER) += fate-eac3-encode
fate-eac3-encode: CMD = enc_dec_pcm eac3 wav s16le $(subst $(SAMPLES),$(TARGET_SAMPLES),$(REF)) -c:a eac3 -b:a 128k
fate-eac3-encode: CMP_SHIFT = -1024
fate-eac3-encode: CMP_TARGET = 516.94
fate-eac3-encode: SIZE_TOLERANCE = 488

fate-ac3-encode fate-eac3-encode: CMP = stddev
fate-ac3-encode fate-eac3-encode: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav

FATE_AC3-$(call ENCMUX, AC3_FIXED, AC3, ARESAMPLE_FILTER) += fate-ac3-fixed-encode
fate-ac3-fixed-encode: tests/data/asynth-44100-2.wav
fate-ac3-fixed-encode: SRC = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
fate-ac3-fixed-encode: CMD = md5 -i $(SRC) -c ac3_fixed -ab 128k -f ac3 -flags +bitexact -af aresample
fate-ac3-fixed-encode: CMP = oneline
fate-ac3-fixed-encode: REF = e9d78bca187b4bbafc4512bcea8efd3e

# This tests that the LFE does not get lost when converting the input 7.1
# to a channel layout supported by the encoder.
FATE_AC3-$(call FRAMECRC, WAV, PCM_S16LE, ARESAMPLE_FILTER AC3_FIXED_ENCODER) += fate-ac3-fixed-encode-2
fate-ac3-fixed-encode-2: tests/data/asynth-44100-8.wav
fate-ac3-fixed-encode-2: SRC = $(TARGET_PATH)/tests/data/asynth-44100-8.wav
fate-ac3-fixed-encode-2: CMD = framecrc -i $(SRC) -c:a ac3_fixed -ab 256k -frames:a 6 -af aresample

FATE_EAC3-$(call ALLYES, EAC3_DEMUXER EAC3_MUXER EAC3_CORE_BSF) += fate-eac3-core-bsf
fate-eac3-core-bsf: CMD = md5pipe -i $(TARGET_SAMPLES)/eac3/the_great_wall_7.1.eac3 -c:a copy -bsf:a eac3_core -fflags +bitexact -f eac3
fate-eac3-core-bsf: CMP = oneline
fate-eac3-core-bsf: REF = b704bf851e99b7442e9bed368b60e6ca

FATE_SAMPLES_AVCONV += $(FATE_AC3-yes) $(FATE_EAC3-yes)

fate-ac3: $(FATE_AC3-yes) $(FATE_EAC3-yes)
