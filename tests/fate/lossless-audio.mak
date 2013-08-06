FATE_SAMPLES_AVCONV-$(call DEMDEC, MOV, ALAC) += fate-lossless-alac
fate-lossless-alac: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/inside.m4a -f s16le

FATE_SAMPLES_AVCONV-$(call DEMDEC, MLP, MLP) += fate-lossless-meridianaudio
fate-lossless-meridianaudio: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/luckynight-partial.mlp -f s16le

FATE_SAMPLES_AVCONV-$(call DEMDEC, RM, RALF) += fate-ralf
fate-ralf: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/luckynight-partial.rmvb -vn -f s16le

FATE_SAMPLES_AVCONV-$(call DEMDEC, SHORTEN, SHORTEN) += fate-lossless-shorten
fate-lossless-shorten: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/luckynight-partial.shn -f s16le

FATE_SAMPLES_AVCONV-$(call DEMDEC, TAK, TAK) += fate-lossless-tak
fate-lossless-tak: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/luckynight-partial.tak -f s16le
fate-lossless-tak: CMP = oneline
fate-lossless-tak: REF = a28d4e5f2192057f7d4bece870f40bd0

FATE_SAMPLES_AVCONV-$(call DEMDEC, TTA, TTA) += fate-lossless-tta
fate-lossless-tta: CMD = crc -i $(TARGET_SAMPLES)/lossless-audio/inside.tta

FATE_SAMPLES_AVCONV-$(call DEMDEC, ASF, WMALOSSLESS) += fate-lossless-wma
fate-lossless-wma: CMD = md5 -i $(TARGET_SAMPLES)/lossless-audio/luckynight-partial.wma -f s16le
