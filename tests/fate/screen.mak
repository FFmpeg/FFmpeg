# FIXME dropped frames in this test because of coarse timebase
FATE_SCREEN-$(call FRAMECRC, AVI, CSCD, SCALE_FILTER) += fate-cscd
fate-cscd: CMD = framecrc -i $(TARGET_SAMPLES)/CSCD/sample_video.avi -an -pix_fmt rgb24 -vf scale

FATE_SCREEN-$(call FRAMECRC, AVI, DXTORY) += fate-dxtory
fate-dxtory: CMD = framecrc -i $(TARGET_SAMPLES)/dxtory/dxtory_mic.avi -an

FATE_SCREEN-$(call FRAMECRC, AVI, FIC) += fate-fic-avi
fate-fic-avi: CMD = framecrc -i $(TARGET_SAMPLES)/fic/fic-partial-2MB.avi -an

FATE_FMVC += fate-fmvc-type1
fate-fmvc-type1: CMD = framecrc -i $(TARGET_SAMPLES)/fmvc/6-methyl-5-hepten-2-one-CC-db_small.avi

FATE_FMVC += fate-fmvc-type2
fate-fmvc-type2: CMD = framecrc -i $(TARGET_SAMPLES)/fmvc/fmvcVirtualDub_small.avi

FATE_FMVC-$(call FRAMECRC, AVI, FMVC) += $(FATE_FMVC)
FATE_SCREEN += $(FATE_FMVC-yes)
fate-fmvc: $(FATE_FMVC-yes)

FATE_FRAPS += fate-fraps-v0
fate-fraps-v0: CMD = framecrc -i $(TARGET_SAMPLES)/fraps/Griffin_Ragdoll01-partial.avi

FATE_FRAPS += fate-fraps-v1
fate-fraps-v1: CMD = framecrc -i $(TARGET_SAMPLES)/fraps/sample-v1.avi -an

FATE_FRAPS += fate-fraps-v2
fate-fraps-v2: CMD = framecrc -i $(TARGET_SAMPLES)/fraps/test3-nosound-partial.avi

FATE_FRAPS-$(call FRAMECRC, AVI, FRAPS, SCALE_FILTER) += fate-fraps-v3
fate-fraps-v3: CMD = framecrc -i $(TARGET_SAMPLES)/fraps/psclient-partial.avi -pix_fmt rgb24 -vf scale

FATE_FRAPS += fate-fraps-v4
fate-fraps-v4: CMD = framecrc -i $(TARGET_SAMPLES)/fraps/WoW_2006-11-03_14-58-17-19-nosound-partial.avi

FATE_FRAPS += fate-fraps-v5
fate-fraps-v5: CMD = framecrc -i $(TARGET_SAMPLES)/fraps/fraps-v5-bouncing-balls-partial.avi

FATE_FRAPS-$(call FRAMECRC, AVI, FRAPS) += $(FATE_FRAPS)
FATE_SCREEN += $(FATE_FRAPS-yes)
fate-fraps: $(FATE_FRAPS-yes)

FATE_G2M += fate-g2m2
fate-g2m2: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/g2m/g2m2.asf -an

FATE_G2M += fate-g2m3
fate-g2m3: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/g2m/g2m3.asf -frames:v 20 -an

FATE_G2M += fate-g2m4
fate-g2m4: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/g2m/g2m4.asf

FATE_SCREEN-$(call FRAMECRC, ASF, G2M) += $(FATE_G2M)
fate-g2m: $(FATE_G2M)

FATE_RSCC += fate-iscc
fate-iscc: CMD = framecrc -i $(TARGET_SAMPLES)/rscc/pip.avi -an

FATE_RSCC-$(call FRAMECRC, AVI, RSCC, SCALE_FILTER) += fate-rscc-8bit
fate-rscc-8bit: CMD = framecrc -i $(TARGET_SAMPLES)/rscc/8bpp.avi -an -pix_fmt rgb24 -vf scale

FATE_RSCC += fate-rscc-16bit
fate-rscc-16bit: CMD = framecrc -i $(TARGET_SAMPLES)/rscc/16bpp_555.avi -an

FATE_RSCC += fate-rscc-24bit
fate-rscc-24bit: CMD = framecrc -i $(TARGET_SAMPLES)/rscc/24bpp.avi -an

FATE_RSCC += fate-rscc-32bit
fate-rscc-32bit: CMD = framecrc -i $(TARGET_SAMPLES)/rscc/32bpp.avi -an

FATE_RSCC-$(call FRAMECRC, AVI, RSCC) += $(FATE_RSCC)
FATE_SCREEN += $(FATE_RSCC-yes)
fate-rscc: $(FATE_RSCC-yes)

FATE_SCREENPRESSO += fate-screenpresso-16bit
fate-screenpresso-16bit: CMD = framecrc -i $(TARGET_SAMPLES)/spv1/16bpp_555.avi -an

FATE_SCREENPRESSO += fate-screenpresso-24bit
fate-screenpresso-24bit: CMD = framecrc -i $(TARGET_SAMPLES)/spv1/bunny.avi -an

FATE_SCREENPRESSO += fate-screenpresso-32bit
fate-screenpresso-32bit: CMD = framecrc -i $(TARGET_SAMPLES)/spv1/32bpp.avi -an

FATE_SCREENPRESSO-$(call FRAMECRC, AVI, SCREENPRESSO) += $(FATE_SCREENPRESSO)
FATE_SCREEN += $(FATE_SCREENPRESSO-yes)
fate-screenpresso: $(FATE_SCREENPRESSO-yes)

FATE_SCREEN-$(call FRAMECRC, ASF, TDSC) += fate-tdsc
fate-tdsc: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/tdsc/tdsc.asf -an -pix_fmt bgr24

FATE_TSCC += fate-tscc-15bit
fate-tscc-15bit: CMD = framecrc -i $(TARGET_SAMPLES)/tscc/oneminute.avi -t 15 -pix_fmt rgb24 -vf scale

FATE_TSCC += fate-tscc-32bit
fate-tscc-32bit: CMD = framecrc -i $(TARGET_SAMPLES)/tscc/2004-12-17-uebung9-partial.avi -pix_fmt rgb24 -an -vf scale

FATE_TSCC-$(call FRAMECRC, AVI, TSCC, SCALE_FILTER) += $(FATE_TSCC)
FATE_SCREEN += $(FATE_TSCC-yes)
fate-tscc: $(FATE_TSCC-yes)

FATE_TSCC2-$(call FRAMECRC, AVI, TSCC2) += fate-tscc2-avi
fate-tscc2-avi: CMD = framecrc -i $(TARGET_SAMPLES)/tscc/tsc2_16bpp.avi

FATE_TSCC2-$(call FRAMECRC, MOV, TSCC2) += fate-tscc2-mov
fate-tscc2-mov: CMD = framecrc -i $(TARGET_SAMPLES)/tscc/rec.trec

FATE_SCREEN += $(FATE_TSCC2-yes)
fate-tscc2: $(FATE_TSCC2-yes)

FATE_VMNC += fate-vmnc-16bit
fate-vmnc-16bit: CMD = framecrc -i $(TARGET_SAMPLES)/VMnc/test.avi -pix_fmt rgb24 -vf scale

FATE_VMNC += fate-vmnc-32bit
fate-vmnc-32bit: CMD = framecrc -i $(TARGET_SAMPLES)/VMnc/VS2k5DebugDemo-01-partial.avi -pix_fmt rgb24 -vf scale

FATE_VMNC-$(call FRAMECRC, AVI, VMNC, SCALE_FILTER) += $(FATE_VMNC)
FATE_SCREEN += $(FATE_VMNC-yes)
fate-vmnc: $(FATE_VMNC-yes)

FATE_ZMBV += fate-zmbv-8bit
# The last frame is corrupted.
fate-zmbv-8bit: CMD = framecrc -i $(TARGET_SAMPLES)/zmbv/wc2_001-partial.avi -an -frames:v 275 -pix_fmt rgb24 -vf scale

FATE_ZMBV += fate-zmbv-15bit
fate-zmbv-15bit: CMD = framecrc -i $(TARGET_SAMPLES)/zmbv/zmbv_15bit.avi -pix_fmt rgb24 -t 25 -vf scale

FATE_ZMBV += fate-zmbv-16bit
fate-zmbv-16bit: CMD = framecrc -i $(TARGET_SAMPLES)/zmbv/zmbv_16bit.avi -pix_fmt rgb24 -t 25 -vf scale

FATE_ZMBV += fate-zmbv-32bit
fate-zmbv-32bit: CMD = framecrc -i $(TARGET_SAMPLES)/zmbv/zmbv_32bit.avi -pix_fmt rgb24 -t 25 -vf scale

FATE_ZMBV-$(call FRAMECRC, AVI, ZMBV, SCALE_FILTER) += $(FATE_ZMBV)
FATE_SCREEN += $(FATE_ZMBV-yes)
fate-zmbv: $(FATE_ZMBV-yes)

FATE_SCREEN += $(FATE_SCREEN-yes)

FATE_SAMPLES_FFMPEG += $(FATE_SCREEN)
fate-screen: $(FATE_SCREEN)
