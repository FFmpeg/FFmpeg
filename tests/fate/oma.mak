FATE_OMA_FFMPEG-$(call ALLYES, OMA_DEMUXER CRC_MUXER) += fate-oma-demux
fate-oma-demux: CMD = crc -i $(TARGET_SAMPLES)/oma/01-Untitled-partial.oma -c:a copy

FATE_SAMPLES_FFMPEG         += $(FATE_OMA_FFMPEG-yes)
fate-oma: $(FATE_OMA_FFMPEG-yes)
