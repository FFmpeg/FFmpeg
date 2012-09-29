FATE_CLLC += fate-cllc-rgb
fate-cllc-rgb: CMD = framecrc -i $(SAMPLES)/cllc/sample-cllc-rgb.avi

FATE_CLLC += fate-cllc-argb
fate-cllc-argb: CMD = framecrc -i $(SAMPLES)/cllc/sample-cllc-argb.avi

FATE_LOSSLESS_VIDEO += $(FATE_CLLC)
fate-cllc: $(FATE_CLLC)

FATE_LAGARITH += fate-lagarith-rgb24
fate-lagarith-rgb24: CMD = framecrc -i $(SAMPLES)/lagarith/lag-rgb24.avi

FATE_LAGARITH += fate-lagarith-rgb32
fate-lagarith-rgb32: CMD = framecrc -i $(SAMPLES)/lagarith/lag-rgb32.avi -pix_fmt bgra

FATE_LAGARITH += fate-lagarith-yuy2
fate-lagarith-yuy2: CMD = framecrc -i $(SAMPLES)/lagarith/lag-yuy2.avi

FATE_LAGARITH += fate-lagarith-yv12
fate-lagarith-yv12: CMD = framecrc -i $(SAMPLES)/lagarith/lag-yv12.avi

FATE_SAMPLES_AVCONV += $(FATE_LAGARITH)
fate-lagarith: $(FATE_LAGARITH)

FATE_LOCO += fate-loco-rgb
fate-loco-rgb: CMD = framecrc -i $(SAMPLES)/loco/pig-loco-rgb.avi

FATE_LOCO += fate-loco-yuy2
fate-loco-yuy2: CMD = framecrc -i $(SAMPLES)/loco/pig-loco-0.avi

FATE_LOSSLESS_VIDEO += $(FATE_LOCO)
fate-loco: $(FATE_LOCO)

FATE_LOSSLESS_VIDEO += fate-msrle-8bit
fate-msrle-8bit: CMD = framecrc -i $(SAMPLES)/msrle/Search-RLE.avi -pix_fmt rgb24

FATE_LOSSLESS_VIDEO += fate-mszh
fate-mszh: CMD = framecrc -i $(SAMPLES)/lcl/mszh-1frame.avi

FATE_LOSSLESS_VIDEO += fate-vble
fate-vble: CMD = framecrc -i $(SAMPLES)/vble/flowers-partial-2MB.avi

FATE_LOSSLESS_VIDEO-$(CONFIG_ZLIB) += fate-zlib
fate-zlib: CMD = framecrc -i $(SAMPLES)/lcl/zlib-1frame.avi

FATE_LOSSLESS_VIDEO-$(CONFIG_ZLIB) += fate-zerocodec
fate-zerocodec: CMD = framecrc -i $(SAMPLES)/zerocodec/sample-zeco.avi

FATE_LOSSLESS_VIDEO += $(FATE_LOSSLESS_VIDEO-yes)

FATE_SAMPLES_FFMPEG += $(FATE_LOSSLESS_VIDEO)
fate-lossless-video: $(FATE_LOSSLESS_VIDEO)
