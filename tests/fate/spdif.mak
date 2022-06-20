# This padds the AAC frames to 16 bit words (the actual size is
# still available in the ADTS headers).
FATE_SPDIF_REMUX-$(call ALLYES, AAC_DEMUXER AAC_DECODER) += fate-spdif-aac-remux
fate-spdif-aac-remux: CMD = transcode aac $(TARGET_SAMPLES)/aac/foo.aac spdif "-c copy" "-c copy"

FATE_SPDIF_REMUX-$(call ALLYES, AC3_DEMUXER AC3_DECODER) += fate-spdif-ac3-remux
fate-spdif-ac3-remux: CMD = transcode ac3 $(TARGET_SAMPLES)/ac3/monsters_inc_5.1_448_small.ac3 spdif "-c copy" "-c copy"

FATE_SPDIF_REMUX-$(call ALLYES, DTS_DEMUXER DCA_DECODER) += fate-spdif-dca-core-remux
fate-spdif-dca-core-remux: CMD = transcode dts $(TARGET_SAMPLES)/dts/dcadec-suite/core_51_24_48_768_0.dtshd spdif "-c copy" "-c copy"

FATE_SPDIF-$(call DEMMUX, DTSHD, SPDIF) += fate-spdif-dca-core-bswap
fate-spdif-dca-core-bswap: CMD = md5 -i $(TARGET_SAMPLES)/dts/dcadec-suite/core_51_24_48_768_0.dtshd -c copy -spdif_flags +be -f spdif

# Only the core will be transferred, extensions are discarded.
FATE_SPDIF_REMUX-$(call ALLYES, DTS_DEMUXER DCA_DECODER) += fate-spdif-dca-master-core-remux
fate-spdif-dca-master-core-remux: CMD = transcode dts $(TARGET_SAMPLES)/dts/master_audio_7.1_24bit.dts spdif "-c copy" "-c copy"

FATE_SPDIF-$(call DEMMUX, DTS, SPDIF) += fate-spdif-dca-master fate-spdif-dca-master-core
fate-spdif-dca-master:      CMD = md5 -i $(TARGET_SAMPLES)/dts/master_audio_7.1_24bit.dts -c copy -dtshd_rate 192000 -f spdif
# This test uses a too low bitrate and therefore switches to only transmit the core.
fate-spdif-dca-master-core: CMD = md5 -i $(TARGET_SAMPLES)/dts/master_audio_7.1_24bit.dts -c copy -dtshd_rate  96000 -f spdif

FATE_SPDIF-$(call DEMMUX, EAC3, SPDIF) += fate-spdif-eac3
fate-spdif-eac3: CMD = md5 -i $(TARGET_SAMPLES)/eac3/csi_miami_stereo_128_spx.eac3 -c copy -f spdif

FATE_SPDIF_REMUX-$(call ALLYES, EAC3_DEMUXER EAC3_DECODER SPDIF_MUXER SPDIF_DEMUXER) += fate-spdif-eac3-remux
fate-spdif-eac3-remux: CMD = transcode eac3 $(TARGET_SAMPLES)/eac3/csi_miami_stereo_128_spx.eac3 spdif "-c copy" "-c copy"

FATE_SPDIF-$(call DEMMUX, MLP, SPDIF) += fate-spdif-mlp
fate-spdif-mlp: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/luckynight-partial.mlp -c copy -f spdif

# Note: The spdif demuxer marks the generated file as containing MP3.
FATE_SPDIF_REMUX-$(call ALLYES, MPEGTS_DEMUXER MPEGAUDIO_PARSER MP3_DECODER) += fate-spdif-mp2-remux
fate-spdif-mp2-remux: CMD = transcode mpegts $(TARGET_SAMPLES)/mpeg2/xdcam8mp2-1s_small.ts spdif "-map 0:a -c copy" "-c copy"

FATE_SPDIF_REMUX-$(call ALLYES, MP3_DEMUXER MP3_DECODER) += fate-spdif-mp3-remux
fate-spdif-mp3-remux: CMD = transcode mp3 $(TARGET_SAMPLES)/audiomatch/square3.mp3 spdif "-c copy" "-c copy"

FATE_SPDIF-$(call DEMMUX, TRUEHD, SPDIF) += fate-spdif-truehd
fate-spdif-truehd: CMD = md5 -i $(TARGET_SAMPLES)/truehd/atmos.thd -c copy -f spdif

# Make the demuxer support all the formats supported by the muxer
# and switch the md5 tests to remux tests?
FATE_SPDIF-$(call REMUX, SPDIF) += $(FATE_SPDIF_REMUX-yes)
FATE_SAMPLES_FFMPEG += $(FATE_SPDIF-yes)
fate-spdif: $(FATE_SPDIF-yes)
