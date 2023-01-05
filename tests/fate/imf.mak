FATE_IMF += fate-imf-cpl-with-repeat
fate-imf-cpl-with-repeat: CMD = framecrc -i $(TARGET_SAMPLES)/imf/countdown/CPL_bb2ce11c-1bb6-4781-8e69-967183d02b9b.xml -c:v copy

FATE_IMF += fate-imf-cpl-with-audio
fate-imf-cpl-with-audio: CMD = framecrc -i $(TARGET_SAMPLES)/imf/countdown-audio/CPL_688f4f63-a317-4271-99bf-51444ff39c5b.xml -c:a copy

FATE_SAMPLES_FFMPEG-$(CONFIG_IMF_DEMUXER) += $(FATE_IMF)

fate-imfdec: $(FATE_IMF)
