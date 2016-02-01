FATE_DCA-$(call DEMDEC, MPEGTS, DCA) += fate-dca-core
fate-dca-core: CMD = pcm -i $(TARGET_SAMPLES)/dts/dts.ts
fate-dca-core: CMP = oneoff
fate-dca-core: REF = $(SAMPLES)/dts/dts.pcm

FATE_DCA-$(call DEMDEC, DTS, DCA) += fate-dca-xll
fate-dca-xll: CMD = md5 -i $(TARGET_SAMPLES)/dts/master_audio_7.1_24bit.dts -f s24le

FATE_DCA-$(call DEMDEC, DTS, DCA) += fate-dts_es
fate-dts_es: CMD = pcm -i $(TARGET_SAMPLES)/dts/dts_es.dts
fate-dts_es: CMP = oneoff
fate-dts_es: REF = $(SAMPLES)/dts/dts_es_2.pcm

FATE_SAMPLES_AUDIO += $(FATE_DCA-yes)
fate-dca: $(FATE_DCA-yes)
