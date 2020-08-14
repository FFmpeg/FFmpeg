FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += $(FATE_WAVPACK)

# lossless

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-lossless-8bit
fate-wavpack-lossless-8bit: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/lossless/8bit-partial.wv -f s8 -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-lossless-12bit
fate-wavpack-lossless-12bit: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/lossless/12bit-partial.wv -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-lossless-16bit
fate-wavpack-lossless-16bit: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/lossless/16bit-partial.wv -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-lossless-24bit
fate-wavpack-lossless-24bit: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/lossless/24bit-partial.wv -f s24le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-lossless-32bit
fate-wavpack-lossless-32bit: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/lossless/32bit_int-partial.wv -f s32le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-lossless-float
fate-wavpack-lossless-float: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/lossless/32bit_float-partial.wv -f f32le -af aresample

# lossy

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-lossy-8bit
fate-wavpack-lossy-8bit: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/lossy/4.0_8-bit.wv -f s8 -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-lossy-16bit
fate-wavpack-lossy-16bit: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/lossy/4.0_16-bit.wv -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-lossy-24bit
fate-wavpack-lossy-24bit: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/lossy/4.0_24-bit.wv -f s24le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-lossy-32bit
fate-wavpack-lossy-32bit: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/lossy/4.0_32-bit_int.wv -f s32le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-lossy-float
fate-wavpack-lossy-float: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/lossy/2.0_32-bit_float.wv -f f32le -af aresample

# channel configurations

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-channels-monofloat
fate-wavpack-channels-monofloat: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/num_channels/mono_float-partial.wv -f f32le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-channels-monoint
fate-wavpack-channels-monoint: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/num_channels/mono_16bit_int.wv -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-channels-4.0
fate-wavpack-channels-4.0: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/num_channels/edward_4.0_16bit-partial.wv -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-channels-5.1
fate-wavpack-channels-5.1: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/num_channels/panslab_sample_5.1_16bit-partial.wv -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-channels-6.1
fate-wavpack-channels-6.1: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/num_channels/eva_2.22_6.1_16bit-partial.wv -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-channels-7.1
fate-wavpack-channels-7.1: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/num_channels/panslab_sample_7.1_16bit-partial.wv -f s16le -af aresample

# speed modes

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-speed-default
fate-wavpack-speed-default: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/speed_modes/default-partial.wv  -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-speed-fast
fate-wavpack-speed-fast: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/speed_modes/fast-partial.wv  -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-speed-high
fate-wavpack-speed-high: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/speed_modes/high-partial.wv  -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-speed-vhigh
fate-wavpack-speed-vhigh: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/speed_modes/vhigh-partial.wv  -f s16le -af aresample

# special cases

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-clipping
fate-wavpack-clipping: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/special/clipping.wv -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-cuesheet
fate-wavpack-cuesheet: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/special/cue_sheet.wv -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-falsestereo
fate-wavpack-falsestereo: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/special/false_stereo.wv -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, WV, WAVPACK) += fate-wavpack-zerolsbs
fate-wavpack-zerolsbs: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/special/zero_lsbs.wv -f s16le -af aresample

FATE_WAVPACK-$(call DEMDEC, MATROSKA, WAVPACK) += fate-wavpack-matroskamode
fate-wavpack-matroskamode: CMD = md5 -i $(TARGET_SAMPLES)/wavpack/special/matroska_mode.mka -f s16le -af aresample

FATE_WAVPACK-$(call DEMMUX, WV, MATROSKA) += fate-wavpack-matroska_mux-mono
fate-wavpack-matroska_mux-mono: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/num_channels/mono_16bit_int.wv -c copy -fflags +bitexact -f matroska
fate-wavpack-matroska_mux-mono: CMP = oneline
fate-wavpack-matroska_mux-mono: REF = 0a10b6cd4d64c25ced75f52d1c6929a7

FATE_WAVPACK-$(call DEMMUX, WV, MATROSKA) += fate-wavpack-matroska_mux-61
fate-wavpack-matroska_mux-61: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/num_channels/eva_2.22_6.1_16bit-partial.wv -c copy -fflags +bitexact -f matroska
fate-wavpack-matroska_mux-61: CMP = oneline
fate-wavpack-matroska_mux-61: REF = fee4bb1d6cf2d54f78eddc4483e7c990

FATE_SAMPLES_AVCONV += $(FATE_WAVPACK-yes)
fate-wavpack: $(FATE_WAVPACK-yes)
