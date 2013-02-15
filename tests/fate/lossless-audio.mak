FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, MOV, ALAC) += fate-lossless-alac
fate-lossless-alac: CMD = md5 -i $(SAMPLES)/lossless-audio/inside.m4a -f s16le

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, MLP, MLP) += fate-lossless-meridianaudio
fate-lossless-meridianaudio: CMD = md5 -i $(SAMPLES)/lossless-audio/luckynight-partial.mlp -f s16le

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, APE, APE) += fate-lossless-monkeysaudio
fate-lossless-monkeysaudio: CMD = md5 -i $(SAMPLES)/lossless-audio/luckynight-partial.ape -f s16le

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, RM, RALF) += fate-ralf
fate-ralf: CMD = md5 -i $(SAMPLES)/lossless-audio/luckynight-partial.rmvb -vn -f s16le

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, SHORTEN, SHORTEN) += fate-lossless-shorten
fate-lossless-shorten: CMD = md5 -i $(SAMPLES)/lossless-audio/luckynight-partial.shn -f s16le

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, TAK, TAK) += fate-lossless-tak
fate-lossless-tak: CMD = crc -i $(SAMPLES)/lossless-audio/luckynight-partial.tak

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, TTA, TTA) += fate-lossless-tta
fate-lossless-tta: CMD = crc -i $(SAMPLES)/lossless-audio/inside.tta

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, TTA, TTA) += fate-lossless-tta-encrypted
fate-lossless-tta-encrypted: CMD = crc -password ffmpeg -i $(SAMPLES)/lossless-audio/encrypted.tta

FATE_SAMPLES_LOSSLESS_AUDIO-$(call DEMDEC, ASF, WMALOSSLESS) += fate-lossless-wma
fate-lossless-wma: CMD = md5 -i $(SAMPLES)/lossless-audio/luckynight-partial.wma -f s16le

FATE_SAMPLES_LOSSLESS_AUDIO += $(FATE_SAMPLES_LOSSLESS_AUDIO-yes)

FATE_SAMPLES_FFMPEG += $(FATE_SAMPLES_LOSSLESS_AUDIO)
fate-lossless-audio: $(FATE_SAMPLES_LOSSLESS_AUDIO)

