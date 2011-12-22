FATE_LOSSLESS_AUDIO += fate-lossless-appleaudio
fate-lossless-appleaudio: CMD = md5  -i $(SAMPLES)/lossless-audio/inside.m4a -f s16le

FATE_LOSSLESS_AUDIO += fate-lossless-meridianaudio
fate-lossless-meridianaudio: CMD = md5  -i $(SAMPLES)/lossless-audio/luckynight-partial.mlp -f s16le

FATE_LOSSLESS_AUDIO += fate-lossless-monkeysaudio
fate-lossless-monkeysaudio: CMD = md5  -i $(SAMPLES)/lossless-audio/luckynight-partial.ape -f s16le

FATE_LOSSLESS_AUDIO += fate-lossless-shortenaudio
fate-lossless-shortenaudio: CMD = md5  -i $(SAMPLES)/lossless-audio/luckynight-partial.shn -f s16le

FATE_LOSSLESS_AUDIO += fate-lossless-tta
fate-lossless-tta: CMD = crc  -i $(SAMPLES)/lossless-audio/inside.tta

FATE_LOSSLESS_AUDIO += fate-lossless-wavpackaudio
fate-lossless-wavpackaudio: CMD = md5  -i $(SAMPLES)/lossless-audio/luckynight-partial.wv -f s16le

FATE_TESTS += $(FATE_LOSSLESS_AUDIO)
fate-lossless-audio: $(FATE_LOSSLESS_AUDIO)
