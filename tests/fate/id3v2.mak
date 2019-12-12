FATE_SAMPLES_ID3V2-$(CONFIG_MP3_DEMUXER) += fate-id3v2-priv
fate-id3v2-priv: CMD = probetags $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3

FATE_SAMPLES_FFPROBE += $(FATE_SAMPLES_ID3V2-yes)
fate-id3v2: $(FATE_SAMPLES_ID3V2-yes)
