FATE_CLLC += fate-cllc-argb
fate-cllc-argb: CMD = framecrc -i $(TARGET_SAMPLES)/cllc/sample-cllc-argb.avi

FATE_CLLC += fate-cllc-rgb
fate-cllc-rgb: CMD = framecrc -i $(TARGET_SAMPLES)/cllc/sample-cllc-rgb.avi

FATE_CLLC += fate-cllc-yuy2-noblock
fate-cllc-yuy2-noblock: CMD = framecrc -i $(TARGET_SAMPLES)/cllc/sample-cllc-yuy2-noblock.avi

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, CLLC) += $(FATE_CLLC)
fate-cllc: $(FATE_CLLC)

FATE_LAGARITH += fate-lagarith-rgb24
fate-lagarith-rgb24: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lag-rgb24.avi

FATE_LAGARITH += fate-lagarith-rgb32
fate-lagarith-rgb32: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lag-rgb32.avi -pix_fmt bgra

FATE_LAGARITH += fate-lagarith-yuy2
fate-lagarith-yuy2: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lag-yuy2.avi

FATE_LAGARITH += fate-lagarith-yv12
fate-lagarith-yv12: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lag-yv12.avi

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, LAGARITH) += $(FATE_LAGARITH)
fate-lagarith: $(FATE_LAGARITH)

FATE_LOCO += fate-loco-rgb
fate-loco-rgb: CMD = framecrc -i $(TARGET_SAMPLES)/loco/pig-loco-rgb.avi

FATE_LOCO += fate-loco-yuy2
fate-loco-yuy2: CMD = framecrc -i $(TARGET_SAMPLES)/loco/pig-loco-0.avi

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, LOCO) += $(FATE_LOCO)
fate-loco: $(FATE_LOCO)

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, MSRLE) += fate-msrle-8bit
fate-msrle-8bit: CMD = framecrc -i $(TARGET_SAMPLES)/msrle/Search-RLE.avi -pix_fmt rgb24

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, MSZH) += fate-mszh
fate-mszh: CMD = framecrc -i $(TARGET_SAMPLES)/lcl/mszh-1frame.avi

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, VBLE) += fate-vble
fate-vble: CMD = framecrc -i $(TARGET_SAMPLES)/vble/flowers-partial-2MB.avi

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, ZEROCODEC) += fate-zerocodec
fate-zerocodec: CMD = framecrc -i $(TARGET_SAMPLES)/zerocodec/sample-zeco.avi

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, ZLIB) += fate-zlib
fate-zlib: CMD = framecrc -i $(TARGET_SAMPLES)/lcl/zlib-1frame.avi
