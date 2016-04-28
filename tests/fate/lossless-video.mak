FATE_CANOPUS_CLLC += fate-canopus-cllc-argb
fate-canopus-cllc-argb: CMD = framecrc -i $(TARGET_SAMPLES)/cllc/sample-cllc-argb.avi

FATE_CANOPUS_CLLC += fate-canopus-cllc-rgb
fate-canopus-cllc-rgb: CMD = framecrc -i $(TARGET_SAMPLES)/cllc/sample-cllc-rgb.avi

FATE_CANOPUS_CLLC += fate-canopus-cllc-yuy2-noblock
fate-canopus-cllc-yuy2-noblock: CMD = framecrc -i $(TARGET_SAMPLES)/cllc/sample-cllc-yuy2-noblock.avi

FATE_LOSSLESS_VIDEO-$(call DEMDEC, AVI, CLLC) += $(FATE_CANOPUS_CLLC)
fate-canopus-cllc: $(FATE_CANOPUS_CLLC)

FATE_LAGARITH += fate-lagarith-rgb24
fate-lagarith-rgb24: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lag-rgb24.avi

FATE_LAGARITH += fate-lagarith-rgb32
fate-lagarith-rgb32: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lag-rgb32.avi -pix_fmt bgra

FATE_LAGARITH += fate-lagarith-yuy2
fate-lagarith-yuy2: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lag-yuy2.avi

FATE_LAGARITH += fate-lagarith-yv12
fate-lagarith-yv12: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lag-yv12.avi

FATE_LAGARITH += fate-lagarith-red
fate-lagarith-red: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lagarith-red.avi

FATE_LAGARITH += fate-lagarith-ticket4119 fate-lagarith-ticket4119-cfr fate-lagarith-ticket4119-vfr fate-lagarith-ticket4119-pass fate-lagarith-ticket4119-drop
fate-lagarith-ticket4119: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lagarith-1.3.27-black-frames-and-off-by-ones.avi
fate-lagarith-ticket4119-cfr : CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lagarith-1.3.27-black-frames-and-off-by-ones.avi -vsync cfr
fate-lagarith-ticket4119-vfr : CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lagarith-1.3.27-black-frames-and-off-by-ones.avi -vsync vfr
fate-lagarith-ticket4119-pass: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lagarith-1.3.27-black-frames-and-off-by-ones.avi -vsync passthrough
fate-lagarith-ticket4119-drop: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lagarith-1.3.27-black-frames-and-off-by-ones.avi -vsync drop

FATE_LOSSLESS_VIDEO-$(call DEMDEC, AVI, LAGARITH) += $(FATE_LAGARITH)
fate-lagarith: $(FATE_LAGARITH)

FATE_LOCO += fate-loco-rgb
fate-loco-rgb: CMD = framecrc -i $(TARGET_SAMPLES)/loco/pig-loco-rgb.avi

FATE_LOCO += fate-loco-yuy2
fate-loco-yuy2: CMD = framecrc -i $(TARGET_SAMPLES)/loco/pig-loco-0.avi

FATE_LOSSLESS_VIDEO-$(call DEMDEC, AVI, LOCO) += $(FATE_LOCO)
fate-loco: $(FATE_LOCO)

FATE_LOSSLESS_VIDEO-$(call DEMDEC, AVI, MSRLE) += fate-msrle-8bit
fate-msrle-8bit: CMD = framecrc -i $(TARGET_SAMPLES)/msrle/Search-RLE.avi -pix_fmt rgb24

FATE_LOSSLESS_VIDEO-$(call DEMDEC, AVI, MSZH) += fate-mszh
fate-mszh: CMD = framecrc -i $(TARGET_SAMPLES)/lcl/mszh-1frame.avi

FATE_LOSSLESS_VIDEO-$(call DEMDEC, AVI, VBLE) += fate-vble
fate-vble: CMD = framecrc -i $(TARGET_SAMPLES)/vble/flowers-partial-2MB.avi

FATE_LOSSLESS_VIDEO-$(call DEMDEC, AVI, ZEROCODEC) += fate-zerocodec
fate-zerocodec: CMD = framecrc -i $(TARGET_SAMPLES)/zerocodec/sample-zeco.avi

FATE_LOSSLESS_VIDEO-$(call DEMDEC, AVI, ZLIB) += fate-zlib
fate-zlib: CMD = framecrc -i $(TARGET_SAMPLES)/lcl/zlib-1frame.avi

FATE_LOSSLESS_VIDEO += $(FATE_LOSSLESS_VIDEO-yes)

FATE_SAMPLES_FFMPEG += $(FATE_LOSSLESS_VIDEO)
fate-lossless-video: $(FATE_LOSSLESS_VIDEO)
