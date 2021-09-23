FATE_CAF_FFMPEG-$(call ALLYES, CAF_DEMUXER CRC_MUXER) += fate-caf-demux
fate-caf-demux: CMD = crc -i $(TARGET_SAMPLES)/caf/caf-pcm16.caf -c copy

FATE_SAMPLES_FFMPEG         += $(FATE_CAF_FFMPEG-yes)
fate-caf: $(FATE_CAF_FFMPEG-yes)
