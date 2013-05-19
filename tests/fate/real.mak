FATE_SAMPLES_AVCONV-$(call DEMDEC, RM, RA_144) += fate-ra-144
fate-ra-144: CMD = md5 -i $(TARGET_SAMPLES)/real/ra3_in_rm_file.rm -f s16le

FATE_SAMPLES_AVCONV-$(call DEMDEC, RM, RA_288) += fate-ra-288
fate-ra-288: CMD = pcm -i $(TARGET_SAMPLES)/real/ra_288.rm
fate-ra-288: CMP = oneoff
fate-ra-288: REF = $(SAMPLES)/real/ra_288.pcm
fate-ra-288: FUZZ = 2

FATE_SAMPLES_AVCONV-$(call DEMDEC, RM, COOK) += fate-ra-cook
fate-ra-cook: CMD = pcm -i $(TARGET_SAMPLES)/real/ra_cook.rm
fate-ra-cook: CMP = oneoff
fate-ra-cook: REF = $(SAMPLES)/real/ra_cook.pcm

FATE_SAMPLES_AVCONV-$(call DEMDEC, RM, RV30) += fate-rv30
fate-rv30: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/real/rv30.rm -an

FATE_SAMPLES_AVCONV-$(call DEMDEC, RM, RV40) += fate-rv40
fate-rv40: CMD = framecrc -i $(TARGET_SAMPLES)/real/spygames-2MB.rmvb -t 10 -an -vsync 0

FATE_SIPR += fate-sipr-5k0
fate-sipr-5k0: CMD = pcm -i $(TARGET_SAMPLES)/sipr/sipr_5k0.rm
fate-sipr-5k0: REF = $(SAMPLES)/sipr/sipr_5k0.pcm

FATE_SIPR += fate-sipr-6k5
fate-sipr-6k5: CMD = pcm -i $(TARGET_SAMPLES)/sipr/sipr_6k5.rm
fate-sipr-6k5: REF = $(SAMPLES)/sipr/sipr_6k5.pcm

FATE_SIPR += fate-sipr-8k5
fate-sipr-8k5: CMD = pcm -i $(TARGET_SAMPLES)/sipr/sipr_8k5.rm
fate-sipr-8k5: REF = $(SAMPLES)/sipr/sipr_8k5.pcm

FATE_SIPR += fate-sipr-16k
fate-sipr-16k: CMD = pcm -i $(TARGET_SAMPLES)/sipr/sipr_16k.rm
fate-sipr-16k: REF = $(SAMPLES)/sipr/sipr_16k.pcm

$(FATE_SIPR): CMP = oneoff

FATE_SAMPLES_AVCONV-$(call DEMDEC, RM, SIPR) += $(FATE_SIPR)
fate-sipr: $(FATE_SIPR)
