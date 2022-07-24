FATE_IMF += fate-imf-cpl-with-repeat
fate-imf-cpl-with-repeat: CMD = framecrc -f imf -i $(TARGET_SAMPLES)/imf/countdown/CPL_bb2ce11c-1bb6-4781-8e69-967183d02b9b.xml -c:v copy

FATE_SAMPLES_FFMPEG-$(CONFIG_IMF_DEMUXER) += $(FATE_IMF)

fate-imfdec: $(FATE_IMF)
