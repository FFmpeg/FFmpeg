FATE_MAPCHAN += fate-mapchan-6ch-extract-2
fate-mapchan-6ch-extract-2: tests/data/mapchan-6ch.sw
fate-mapchan-6ch-extract-2: CMD = avconv -ar 22050 -ac 6 -f s16le -i $(TARGET_PATH)/tests/data/mapchan-6ch.sw -map_channel 0.0.0 -f wav md5: -map_channel 0.0.1 -f wav md5:

FATE_MAPCHAN += fate-mapchan-6ch-extract-2-downmix-mono
fate-mapchan-6ch-extract-2-downmix-mono: tests/data/mapchan-6ch.sw
fate-mapchan-6ch-extract-2-downmix-mono: CMD = md5 -ar 22050 -ac 6 -f s16le -i $(TARGET_PATH)/tests/data/mapchan-6ch.sw -map_channel 0.0.1 -map_channel 0.0.0 -ac 1 -f wav

FATE_MAPCHAN += fate-mapchan-silent-mono
fate-mapchan-silent-mono: tests/data/mapchan-mono.sw
fate-mapchan-silent-mono: CMD = md5 -ar 22050 -ac 1 -f s16le -i $(TARGET_PATH)/tests/data/mapchan-mono.sw -map_channel -1 -map_channel 0.0.0 -f wav

FATE_TESTS += $(FATE_MAPCHAN)
fate-mapchan: $(FATE_MAPCHAN)
