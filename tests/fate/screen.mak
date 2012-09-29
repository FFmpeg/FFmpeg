# FIXME dropped frames in this test because of coarse timebase
FATE_SCREEN += fate-cscd
fate-cscd: CMD = framecrc -i $(SAMPLES)/CSCD/sample_video.avi -an -pix_fmt rgb24

FATE_SCREEN += fate-dxtory
fate-dxtory: CMD = framecrc -i $(SAMPLES)/dxtory/dxtory_mic.avi

FATE_FRAPS += fate-fraps-v0
fate-fraps-v0: CMD = framecrc -i $(SAMPLES)/fraps/Griffin_Ragdoll01-partial.avi

FATE_FRAPS += fate-fraps-v1
fate-fraps-v1: CMD = framecrc -i $(SAMPLES)/fraps/sample-v1.avi -an

FATE_FRAPS += fate-fraps-v2
fate-fraps-v2: CMD = framecrc -i $(SAMPLES)/fraps/test3-nosound-partial.avi

FATE_FRAPS += fate-fraps-v3
fate-fraps-v3: CMD = framecrc -i $(SAMPLES)/fraps/psclient-partial.avi -pix_fmt rgb24

FATE_FRAPS += fate-fraps-v4
fate-fraps-v4: CMD = framecrc -i $(SAMPLES)/fraps/WoW_2006-11-03_14-58-17-19-nosound-partial.avi

FATE_FRAPS += fate-fraps-v5
fate-fraps-v5: CMD = framecrc -i $(SAMPLES)/fraps/fraps-v5-bouncing-balls-partial.avi

FATE_SCREEN += $(FATE_FRAPS)
fate-fraps: $(FATE_FRAPS)

FATE_TSCC += fate-tscc-15bit
fate-tscc-15bit: CMD = framecrc -i $(SAMPLES)/tscc/oneminute.avi -t 15 -pix_fmt rgb24

FATE_TSCC += fate-tscc-32bit
fate-tscc-32bit: CMD = framecrc -i $(SAMPLES)/tscc/2004-12-17-uebung9-partial.avi -pix_fmt rgb24 -an

FATE_SCREEN-$(CONFIG_ZLIB) += $(FATE_TSCC)
fate-tscc: $(FATE_TSCC)

FATE_VMNC += fate-vmnc-16bit
fate-vmnc-16bit: CMD = framecrc -i $(SAMPLES)/VMnc/test.avi -pix_fmt rgb24

FATE_VMNC += fate-vmnc-32bit
fate-vmnc-32bit: CMD = framecrc -i $(SAMPLES)/VMnc/VS2k5DebugDemo-01-partial.avi -pix_fmt rgb24

FATE_SCREEN += $(FATE_VMNC)
fate-vmnc: $(FATE_VMNC)

FATE_ZMBV += fate-zmbv-8bit
fate-zmbv-8bit: CMD = framecrc -i $(SAMPLES)/zmbv/wc2_001-partial.avi -an -pix_fmt rgb24

FATE_ZMBV += fate-zmbv-15bit
fate-zmbv-15bit: CMD = framecrc -i $(SAMPLES)/zmbv/zmbv_15bit.avi -pix_fmt rgb24 -t 25

FATE_ZMBV += fate-zmbv-16bit
fate-zmbv-16bit: CMD = framecrc -i $(SAMPLES)/zmbv/zmbv_16bit.avi -pix_fmt rgb24 -t 25

FATE_ZMBV += fate-zmbv-32bit
fate-zmbv-32bit: CMD = framecrc -i $(SAMPLES)/zmbv/zmbv_32bit.avi -pix_fmt rgb24 -t 25

FATE_SCREEN-$(CONFIG_ZLIB) += $(FATE_ZMBV)
fate-zmbv: $(FATE_ZMBV)

FATE_SCREEN += $(FATE_SCREEN-yes)

FATE_SAMPLES_FFMPEG += $(FATE_SCREEN)
fate-screen: $(FATE_SCREEN)
