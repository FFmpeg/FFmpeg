# lossless

FATE_WAVPACK_S8 += fate-wavpack-lossless-8bit
fate-wavpack-lossless-8bit: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/lossless/8bit-partial.wv -f s8 -af aresample

FATE_WAVPACK_S16 += fate-wavpack-lossless-12bit
fate-wavpack-lossless-12bit: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/lossless/12bit-partial.wv -f s16le -af aresample

FATE_WAVPACK_S16 += fate-wavpack-lossless-16bit
fate-wavpack-lossless-16bit: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/lossless/16bit-partial.wv -f s16le -af aresample

FATE_WAVPACK_S24 += fate-wavpack-lossless-24bit
fate-wavpack-lossless-24bit: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/lossless/24bit-partial.wv -f s24le -af aresample

FATE_WAVPACK_S32 += fate-wavpack-lossless-32bit
fate-wavpack-lossless-32bit: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/lossless/32bit_int-partial.wv -f s32le -af aresample

FATE_WAVPACK_F32 += fate-wavpack-lossless-float
fate-wavpack-lossless-float: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/lossless/32bit_float-partial.wv -f f32le -af aresample

FATE_WAVPACK_F32 += fate-wavpack-lossless-dsd
fate-wavpack-lossless-dsd: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/lossless/dsd.wv -f f32le -af aresample

# lossy

FATE_WAVPACK_S8 += fate-wavpack-lossy-8bit
fate-wavpack-lossy-8bit: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/lossy/4.0_8-bit.wv -f s8 -af aresample

FATE_WAVPACK_S16 += fate-wavpack-lossy-16bit
fate-wavpack-lossy-16bit: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/lossy/4.0_16-bit.wv -f s16le -af aresample

FATE_WAVPACK_S24 += fate-wavpack-lossy-24bit
fate-wavpack-lossy-24bit: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/lossy/4.0_24-bit.wv -f s24le -af aresample

FATE_WAVPACK_S32 += fate-wavpack-lossy-32bit
fate-wavpack-lossy-32bit: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/lossy/4.0_32-bit_int.wv -f s32le -af aresample

FATE_WAVPACK_F32 += fate-wavpack-lossy-float
fate-wavpack-lossy-float: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/lossy/2.0_32-bit_float.wv -f f32le -af aresample

# channel configurations

FATE_WAVPACK_F32 += fate-wavpack-channels-monofloat
fate-wavpack-channels-monofloat: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/num_channels/mono_float-partial.wv -f f32le -af aresample

FATE_WAVPACK_S16 += fate-wavpack-channels-monoint
fate-wavpack-channels-monoint: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/num_channels/mono_16bit_int.wv -f s16le -af aresample

FATE_WAVPACK_S16 += fate-wavpack-channels-4.0
fate-wavpack-channels-4.0: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/num_channels/edward_4.0_16bit-partial.wv -f s16le -af aresample

FATE_WAVPACK_S16 += fate-wavpack-channels-5.1
fate-wavpack-channels-5.1: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/num_channels/panslab_sample_5.1_16bit-partial.wv -f s16le -af aresample

FATE_WAVPACK_S16 += fate-wavpack-channels-6.1
fate-wavpack-channels-6.1: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/num_channels/eva_2.22_6.1_16bit-partial.wv -f s16le -af aresample

FATE_WAVPACK_S16 += fate-wavpack-channels-7.1
fate-wavpack-channels-7.1: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/num_channels/panslab_sample_7.1_16bit-partial.wv -f s16le -af aresample

# speed modes

FATE_WAVPACK_S16 += fate-wavpack-speed-default
fate-wavpack-speed-default: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/speed_modes/default-partial.wv  -f s16le -af aresample

FATE_WAVPACK_S16 += fate-wavpack-speed-fast
fate-wavpack-speed-fast: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/speed_modes/fast-partial.wv  -f s16le -af aresample

FATE_WAVPACK_S16 += fate-wavpack-speed-high
fate-wavpack-speed-high: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/speed_modes/high-partial.wv  -f s16le -af aresample

FATE_WAVPACK_S16 += fate-wavpack-speed-vhigh
fate-wavpack-speed-vhigh: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/speed_modes/vhigh-partial.wv  -f s16le -af aresample

# special cases

FATE_WAVPACK_S16 += fate-wavpack-clipping
fate-wavpack-clipping: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/special/clipping.wv -f s16le -af aresample

FATE_WAVPACK_S16 += fate-wavpack-cuesheet
fate-wavpack-cuesheet: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/special/cue_sheet.wv -f s16le -af aresample

# The sample file has APE tags containing a cuesheet.
FATE_WAVPACK_FFPROBE-$(call ALLYES, WV_DEMUXER FILE_PROTOCOL) += fate-wavpack-cuesheet-tags
fate-wavpack-cuesheet-tags: CMD = probetags $(TARGET_SAMPLES)/wavpack/special/cue_sheet.wv

FATE_WAVPACK_S16 += fate-wavpack-falsestereo
fate-wavpack-falsestereo: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/special/false_stereo.wv -f s16le -af aresample

FATE_WAVPACK_S16 += fate-wavpack-zerolsbs
fate-wavpack-zerolsbs: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/special/zero_lsbs.wv -f s16le -af aresample

FATE_WAVPACK-$(call FILTERDEMDECENCMUX, ARESAMPLE, MATROSKA, WAVPACK, PCM_S16LE, PCM_S16LE, MD5_PROTOCOL) += fate-wavpack-matroskamode
fate-wavpack-matroskamode: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/special/matroska_mode.mka -f s16le -af aresample

FATE_WAVPACK-$(call DEMMUX, WV, MATROSKA, MD5_PROTOCOL) += fate-wavpack-matroska_mux-mono
fate-wavpack-matroska_mux-mono: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/num_channels/mono_16bit_int.wv -c copy -fflags +bitexact -f matroska
fate-wavpack-matroska_mux-mono: CMP = oneline
fate-wavpack-matroska_mux-mono: REF = 5863eb94f7583aa9c5fded17474f2e7c

FATE_WAVPACK-$(call DEMMUX, WV, MATROSKA, MD5_PROTOCOL) += fate-wavpack-matroska_mux-61
fate-wavpack-matroska_mux-61: CMD = md5pipe -i $(TARGET_SAMPLES)/wavpack/num_channels/eva_2.22_6.1_16bit-partial.wv -c copy -fflags +bitexact -f matroska
fate-wavpack-matroska_mux-61: CMP = oneline
fate-wavpack-matroska_mux-61: REF = 2bbb393d060fa841f1172028f2c69b3a

FATE_WAVPACK-$(call FILTERDEMDECENCMUX, ARESAMPLE, WV, WAVPACK, PCM_S8,    PCM_S8,    MD5_PROTOCOL) += $(FATE_WAVPACK_S8)
FATE_WAVPACK-$(call FILTERDEMDECENCMUX, ARESAMPLE, WV, WAVPACK, PCM_S16LE, PCM_S16LE, MD5_PROTOCOL) += $(FATE_WAVPACK_S16)
FATE_WAVPACK-$(call FILTERDEMDECENCMUX, ARESAMPLE, WV, WAVPACK, PCM_S24LE, PCM_S24LE, MD5_PROTOCOL) += $(FATE_WAVPACK_S24)
FATE_WAVPACK-$(call FILTERDEMDECENCMUX, ARESAMPLE, WV, WAVPACK, PCM_S32LE, PCM_S32LE, MD5_PROTOCOL) += $(FATE_WAVPACK_S32)
FATE_WAVPACK-$(call FILTERDEMDECENCMUX, ARESAMPLE, WV, WAVPACK, PCM_F32LE, PCM_F32LE, MD5_PROTOCOL) += $(FATE_WAVPACK_F32)

FATE_SAMPLES_FFPROBE += $(FATE_WAVPACK_FFPROBE-yes)
FATE_SAMPLES_FFMPEG += $(FATE_WAVPACK-yes)
fate-wavpack: $(FATE_WAVPACK-yes) $(FATE_WAVPACK_FFPROBE-yes)
