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

# the fixed decoder has to keep the overlap of the independent substream when
# the dependent substream uses a different coefficient format
FATE_EAC3_FIXED += fate-eac3-fixed-dependent-substream
fate-eac3-fixed-dependent-substream: CMD = pcm -c ac3_fixed -i $(TARGET_SAMPLES)/eac3/the_great_wall_7.1.eac3
fate-eac3-fixed-dependent-substream: REF = $(SAMPLES)/eac3/the_great_wall_7.1.pcm

$(FATE_AC3) $(FATE_EAC3) $(FATE_EAC3_FIXED): CMP = oneoff

# the references were generated with the truncating dequantization
# (mantissa >> exp) that the float decoder used to share with ac3_fixed.
# the float decoder now dequantizes exactly (mantissa * 2^-exp), which
# moves its output by up to 1 s16 unit on these streams, except a measured
# MAXDIFF of 2 on fate-ac3-2.0 and fate-ac3-5.1.
fate-ac3-2.0: FUZZ = 2
fate-ac3-5.1: FUZZ = 2

FATE_AC3-$(call  PCM, AC3,  AC3 AC3_FIXED, PCM_S16LE_MUXER ARESAMPLE_FILTER)  += $(FATE_AC3)
FATE_EAC3-$(call PCM, EAC3, EAC3,          PCM_S16LE_MUXER ARESAMPLE_FILTER) += $(FATE_EAC3)
FATE_EAC3-$(call PCM, EAC3, EAC3 AC3_FIXED, PCM_S16LE_MUXER ARESAMPLE_FILTER) += $(FATE_EAC3_FIXED)

FATE_AC3-$(call ENCDEC, AC3, MP4 MOV, WAV_MUXER WAV_DEMUXER ARESAMPLE_FILTER PCM_S16LE_ENCODER PIPE_PROTOCOL) += fate-ac3-encode
fate-ac3-encode: CMD = enc_dec_pcm mp4 wav s16le $(subst $(SAMPLES),$(TARGET_SAMPLES),$(REF)) -c:a ac3 -b:a 128k
fate-ac3-encode: CMP_TARGET = 404.53


FATE_EAC3-$(call ENCDEC, EAC3, MP4 MOV, WAV_MUXER WAV_DEMUXER ARESAMPLE_FILTER PCM_S16LE_ENCODER PIPE_PROTOCOL) += fate-eac3-encode
fate-eac3-encode: CMD = enc_dec_pcm mp4 wav s16le $(subst $(SAMPLES),$(TARGET_SAMPLES),$(REF)) -c:a eac3 -b:a 128k
fate-eac3-encode: CMP_TARGET = 516.94

fate-ac3-encode fate-eac3-encode: CMP = stddev
fate-ac3-encode fate-eac3-encode: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav

FATE_AC3-$(call ENCMUX, AC3_FIXED, AC3, WAV_DEMUXER PCM_S16LE_DECODER ARESAMPLE_FILTER) += fate-ac3-fixed-encode
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

# This tests that all samples are output and that audio frame queue API
# takes into account the padding added in the generic encode framework
# by the fixed_frame_size flag.
FATE_AC3-$(call FRAMECRC, WAV, PCM_S16LE, ARESAMPLE_FILTER AC3_FIXED_ENCODER) += fate-ac3-fixed-encode-3
fate-ac3-fixed-encode-3: tests/data/asynth-44100-6.wav
fate-ac3-fixed-encode-3: SRC = $(TARGET_PATH)/tests/data/asynth-44100-6.wav
fate-ac3-fixed-encode-3: CMD = framecrc -i $(SRC) -c:a ac3_fixed -flags2 +fixed_frame_size -ab 256k -af aresample,atrim=start_sample=0:end_sample=12096

# With coupling and rematrixing disabled, this produces bap=0, dexp=24 bins
# whose dither affects the decoded output.
tests/data/fate/ac3-fixed-dexp24.ac3: TAG = GEN
tests/data/fate/ac3-fixed-dexp24.ac3: tests/data/asynth-44100-2.wav
tests/data/fate/ac3-fixed-dexp24.ac3: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data/fate
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< \
	-hide_banner -loglevel error -nostdin \
	-i $(TARGET_PATH)/tests/data/asynth-44100-2.wav \
	-c:a ac3_fixed -b:a 192k \
	-channel_coupling 0 -stereo_rematrixing 0 -flags +bitexact \
	-frames:a 173 -f ac3 -y $(TARGET_PATH)/$@

FATE_AC3_FIXED_DEXP24-$(call ALLYES, FFMPEG WAV_DEMUXER ARESAMPLE_FILTER ATRIM_FILTER \
                                    AC3_FIXED_ENCODER FRAMECRC_MUXER \
                                    AC3_MUXER AC3_DEMUXER AC3_FIXED_DECODER \
                                    PCM_S16LE_DECODER PCM_S16LE_ENCODER \
                                    FILE_PROTOCOL) += fate-ac3-fixed-dexp24
fate-ac3-fixed-dexp24: tests/data/fate/ac3-fixed-dexp24.ac3
fate-ac3-fixed-dexp24: CMD = framecrc -auto_conversion_filters -c ac3_fixed \
                                   -i $(TARGET_PATH)/tests/data/fate/ac3-fixed-dexp24.ac3 \
                                   -af atrim=start_sample=264192

# digital silence encoded with the bitexact fixed-point encoder must decode
# to pure dither noise at the level mandated by the spec (mantissa * 2^-exp).
# The former truncating dequantization (mantissa >> exp) collapsed the dither
# mantissas to coarse {-1, 0} steps, raising the decoded noise floor by a
# factor of ~7 (stddev 18.22 instead of 2.69, tiny_psnr f32 units).
tests/data/fate/ac3-silence.ac3: TAG = GEN
tests/data/fate/ac3-silence.ac3: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data/fate
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i anullsrc=r=44100:cl=stereo -t 1 \
	-c:a ac3_fixed -b:a 192k -flags +bitexact -f ac3 -y $(TARGET_PATH)/$@ 2>/dev/null

tests/data/fate/ac3-silence.f32: TAG = GEN
tests/data/fate/ac3-silence.f32: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data/fate
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	-f lavfi -i anullsrc=r=44100:cl=stereo -af atrim=end_sample=44100 \
	-f f32le -y $(TARGET_PATH)/$@ 2>/dev/null

FATE_AC3_DITHER-$(call ALLYES, FFMPEG LAVFI_INDEV ANULLSRC_FILTER ATRIM_FILTER \
                               ARESAMPLE_FILTER AC3_FIXED_ENCODER AC3_MUXER \
                               AC3_DEMUXER AC3_DECODER PCM_F32LE_ENCODER \
                               PCM_F32LE_MUXER FILE_PROTOCOL PIPE_PROTOCOL) += fate-ac3-float-dither
fate-ac3-float-dither: tests/data/fate/ac3-silence.ac3 tests/data/fate/ac3-silence.f32
fate-ac3-float-dither: CMD = ffmpeg -auto_conversion_filters -cons_noisegen 1 -i $(TARGET_PATH)/tests/data/fate/ac3-silence.ac3 -af atrim=end_sample=44100 -f f32le -
fate-ac3-float-dither: CMP = stddev
fate-ac3-float-dither: CMP_UNIT = f32
fate-ac3-float-dither: REF = tests/data/fate/ac3-silence.f32
fate-ac3-float-dither: CMP_TARGET = 2.69

FATE_EAC3-$(call ALLYES, EAC3_DEMUXER EAC3_MUXER EAC3_CORE_BSF) += fate-eac3-core-bsf
fate-eac3-core-bsf: CMD = md5pipe -i $(TARGET_SAMPLES)/eac3/the_great_wall_7.1.eac3 -c:a copy -bsf:a eac3_core -fflags +bitexact -f eac3
fate-eac3-core-bsf: CMP = oneline
fate-eac3-core-bsf: REF = b704bf851e99b7442e9bed368b60e6ca

FATE_SAMPLES_AVCONV += $(FATE_AC3-yes) $(FATE_EAC3-yes)
FATE_FFMPEG += $(FATE_AC3_DITHER-yes) $(FATE_AC3_FIXED_DEXP24-yes)

fate-ac3: $(FATE_AC3-yes) $(FATE_EAC3-yes) $(FATE_AC3_DITHER-yes) $(FATE_AC3_FIXED_DEXP24-yes)
