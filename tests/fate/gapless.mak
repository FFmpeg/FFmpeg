FATE_GAPLESS-$(CONFIG_MP3_DEMUXER) += fate-gapless-mp3 fate-gapless-mp3-notoc
fate-gapless-mp3: CMD = gapless $(TARGET_SAMPLES)/gapless/gapless.mp3 "-usetoc 1"
fate-gapless-mp3-notoc: CMD = gapless $(TARGET_SAMPLES)/gapless/gapless.mp3 "-usetoc 0"

FATE_GAPLESS = $(FATE_GAPLESS-yes)

FATE_SAMPLES_AVCONV += $(FATE_GAPLESS)
fate-gapless: $(FATE_GAPLESS)
