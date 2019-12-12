FATE_DPCM-$(call DEMDEC, ROQ, ROQ_DPCM) += fate-dpcm-idroq
fate-dpcm-idroq: CMD = framecrc -i $(TARGET_SAMPLES)/idroq/idlogo.roq -vn

FATE_DPCM-$(call DEMDEC, IPMOVIE, INTERPLAY_DPCM) += fate-dpcm-interplay
fate-dpcm-interplay: CMD = framecrc -i $(TARGET_SAMPLES)/interplay-mve/interplay-logo-2MB.mve -vn

FATE_DPCM-$(call DEMDEC, SOL, SOL_DPCM) += fate-dpcm-sierra
fate-dpcm-sierra: CMD = md5 -i $(TARGET_SAMPLES)/sol/lsl7sample.sol -f s16le

FATE_DPCM-$(call DEMDEC, AVI, XAN_DPCM) += fate-dpcm-xan
fate-dpcm-xan: CMD = md5 -i $(TARGET_SAMPLES)/wc4-xan/wc4_2.avi -vn -f s16le

FATE_SAMPLES_AVCONV += $(FATE_DPCM-yes)
fate-dpcm: $(FATE_DPCM-yes)
