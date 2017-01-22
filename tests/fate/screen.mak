# FIXME dropped frames in this test because of coarse timebase
FATE_SCREEN-$(call DEMDEC, AVI, CSCD) += fate-cscd
fate-cscd: CMD = framecrc -i $(TARGET_SAMPLES)/CSCD/sample_video.avi -an -pix_fmt rgb24

FATE_SCREEN-$(call DEMDEC, AVI, DXTORY) += fate-dxtory
fate-dxtory: CMD = framecrc -i $(TARGET_SAMPLES)/dxtory/dxtory_mic.avi -an

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, FIC) += fate-fic-avi
fate-fic-avi: CMD = framecrc -i $(TARGET_SAMPLES)/fic/fic-partial-2MB.avi -an

FATE_FRAPS += fate-fraps-v0
fate-fraps-v0: CMD = framecrc -i $(TARGET_SAMPLES)/fraps/Griffin_Ragdoll01-partial.avi

FATE_FRAPS += fate-fraps-v1
fate-fraps-v1: CMD = framecrc -i $(TARGET_SAMPLES)/fraps/sample-v1.avi -an

FATE_FRAPS += fate-fraps-v2
fate-fraps-v2: CMD = framecrc -i $(TARGET_SAMPLES)/fraps/test3-nosound-partial.avi

FATE_FRAPS += fate-fraps-v3
fate-fraps-v3: CMD = framecrc -i $(TARGET_SAMPLES)/fraps/psclient-partial.avi -pix_fmt rgb24

FATE_FRAPS += fate-fraps-v4
fate-fraps-v4: CMD = framecrc -i $(TARGET_SAMPLES)/fraps/WoW_2006-11-03_14-58-17-19-nosound-partial.avi

FATE_FRAPS += fate-fraps-v5
fate-fraps-v5: CMD = framecrc -i $(TARGET_SAMPLES)/fraps/fraps-v5-bouncing-balls-partial.avi

FATE_SCREEN-$(call DEMDEC, AVI, FRAPS) += $(FATE_FRAPS)
fate-fraps: $(FATE_FRAPS)

FATE_G2M += fate-g2m2
fate-g2m2: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/g2m/g2m2.asf -an

FATE_G2M += fate-g2m3
fate-g2m3: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/g2m/g2m3.asf -frames:v 20 -an

FATE_G2M += fate-g2m4
fate-g2m4: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/g2m/g2m4.asf

FATE_SAMPLES_AVCONV-$(call DEMDEC, ASF, G2M) += $(FATE_G2M)
fate-g2m: $(FATE_G2M)

FATE_RSCC += fate-iscc
fate-iscc: CMD = framecrc -i $(TARGET_SAMPLES)/rscc/pip.avi -an

FATE_RSCC += fate-rscc-16bit
fate-rscc-16bit: CMD = framecrc -i $(TARGET_SAMPLES)/rscc/16bpp_555.avi -an

FATE_RSCC += fate-rscc-24bit
fate-rscc-24bit: CMD = framecrc -i $(TARGET_SAMPLES)/rscc/24bpp.avi -an

FATE_RSCC += fate-rscc-32bit
fate-rscc-32bit: CMD = framecrc -i $(TARGET_SAMPLES)/rscc/32bpp.avi -an

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, RSCC) += $(FATE_RSCC)
fate-rscc: $(FATE_RSCC)

FATE_SCREENPRESSO += fate-screenpresso-16bit
fate-screenpresso-16bit: CMD = framecrc -i $(TARGET_SAMPLES)/spv1/16bpp_555.avi -an

FATE_SCREENPRESSO += fate-screenpresso-24bit
fate-screenpresso-24bit: CMD = framecrc -i $(TARGET_SAMPLES)/spv1/bunny.avi -an

FATE_SCREENPRESSO += fate-screenpresso-32bit
fate-screenpresso-32bit: CMD = framecrc -i $(TARGET_SAMPLES)/spv1/32bpp.avi -an

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, SCREENPRESSO) += $(FATE_SCREENPRESSO)
fate-screenpresso: $(FATE_SCREENPRESSO)

FATE_SAMPLES_AVCONV-$(call DEMDEC, ASF, TDSC) += fate-tdsc
fate-tdsc: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/tdsc/tdsc.asf -an -pix_fmt bgr24

FATE_TSCC += fate-tscc-15bit
fate-tscc-15bit: CMD = framecrc -i $(TARGET_SAMPLES)/tscc/oneminute.avi -t 15 -pix_fmt rgb24

FATE_TSCC += fate-tscc-32bit
fate-tscc-32bit: CMD = framecrc -i $(TARGET_SAMPLES)/tscc/2004-12-17-uebung9-partial.avi -pix_fmt rgb24 -an

FATE_SCREEN-$(call DEMDEC, AVI, TSCC) += $(FATE_TSCC)
fate-tscc: $(FATE_TSCC)

FATE_TSCC2-$(CONFIG_AVI_DEMUXER) += fate-tscc2-avi
fate-tscc2-avi: CMD = framecrc -i $(TARGET_SAMPLES)/tscc/tsc2_16bpp.avi

FATE_TSCC2-$(CONFIG_MOV_DEMUXER) += fate-tscc2-mov
fate-tscc2-mov: CMD = framecrc -i $(TARGET_SAMPLES)/tscc/rec.trec

FATE_SAMPLES_AVCONV-$(CONFIG_TSCC2_DECODER) += $(FATE_TSCC2-yes)
fate-tscc2: $(FATE_TSCC2-yes)

FATE_VMNC += fate-vmnc-16bit
fate-vmnc-16bit: CMD = framecrc -i $(TARGET_SAMPLES)/VMnc/test.avi -pix_fmt rgb24

FATE_VMNC += fate-vmnc-32bit
fate-vmnc-32bit: CMD = framecrc -i $(TARGET_SAMPLES)/VMnc/VS2k5DebugDemo-01-partial.avi -pix_fmt rgb24

FATE_SCREEN-$(call DEMDEC, AVI, VMNC) += $(FATE_VMNC)
fate-vmnc: $(FATE_VMNC)

FATE_ZMBV += fate-zmbv-8bit
fate-zmbv-8bit: CMD = framecrc -i $(TARGET_SAMPLES)/zmbv/wc2_001-partial.avi -an -pix_fmt rgb24

FATE_ZMBV += fate-zmbv-15bit
fate-zmbv-15bit: CMD = framecrc -i $(TARGET_SAMPLES)/zmbv/zmbv_15bit.avi -pix_fmt rgb24 -t 25

FATE_ZMBV += fate-zmbv-16bit
fate-zmbv-16bit: CMD = framecrc -i $(TARGET_SAMPLES)/zmbv/zmbv_16bit.avi -pix_fmt rgb24 -t 25

FATE_ZMBV += fate-zmbv-32bit
fate-zmbv-32bit: CMD = framecrc -i $(TARGET_SAMPLES)/zmbv/zmbv_32bit.avi -pix_fmt rgb24 -t 25

FATE_SCREEN-$(call DEMDEC, AVI, ZMBV) += $(FATE_ZMBV)
fate-zmbv: $(FATE_ZMBV)

FATE_SCREEN += $(FATE_SCREEN-yes)

FATE_SAMPLES_FFMPEG += $(FATE_SCREEN)
fate-screen: $(FATE_SCREEN)
