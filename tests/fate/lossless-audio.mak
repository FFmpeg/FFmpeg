FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, MOV, ALAC) += fate-lossless-alac
fate-lossless-alac: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/inside.m4a -f s16le -af aresample

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, MLP, MLP) += fate-lossless-meridianaudio
fate-lossless-meridianaudio: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/luckynight-partial.mlp -f s16le

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, RM, RALF) += fate-ralf
fate-ralf: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/luckynight-partial.rmvb -vn -f s16le -af aresample

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, SHORTEN, SHORTEN) += fate-lossless-shorten
fate-lossless-shorten: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/luckynight-partial.shn -f s16le -af aresample

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, TAK, TAK) += fate-lossless-tak
fate-lossless-tak: CMD = crc -i $(TARGET_SAMPLES)/lossless-audio/luckynight-partial.tak -af aresample

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, TTA, TTA) += fate-lossless-tta
fate-lossless-tta: CMD = crc -i $(TARGET_SAMPLES)/lossless-audio/inside.tta

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, TTA, TTA) += fate-lossless-tta-encrypted
fate-lossless-tta-encrypted: CMD = crc -password ffmpeg -i $(TARGET_SAMPLES)/lossless-audio/encrypted.tta

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, ASF, WMALOSSLESS) += fate-lossless-wma fate-lossless-wma24-1 fate-lossless-wma24-2 fate-lossless-wma24-rawtile
fate-lossless-wma: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/luckynight-partial.wma -f s16le -frames 209 -af aresample
fate-lossless-wma24-1: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/master_audio_2.0_24bit.wma -f s24le -af aresample
fate-lossless-wma24-2: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/Mega_Weird_Audio_Test_24bit.wma -f s24le -af aresample
fate-lossless-wma24-rawtile: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/g2_24bit.wma -f s24le -af aresample
fate-lossless-wmall: fate-lossless-wma fate-lossless-wma24-1 fate-lossless-wma24-2 fate-lossless-wma24-rawtile

FATE_SAMPLES_LOSSLESS_AUDIO += $(FATE_SAMPLES_LOSSLESS_AUDIO-yes)

FATE_SAMPLES_FFMPEG += $(FATE_SAMPLES_LOSSLESS_AUDIO)
fate-lossless-audio: $(FATE_SAMPLES_LOSSLESS_AUDIO)

