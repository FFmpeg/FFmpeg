FATE_LAGARITH += fate-lagarith-rgb24
fate-lagarith-rgb24: CMD = framecrc -i $(SAMPLES)/lagarith/lag-rgb24.avi

FATE_LAGARITH += fate-lagarith-rgb32
fate-lagarith-rgb32: CMD = framecrc -i $(SAMPLES)/lagarith/lag-rgb32.avi

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

FATE_SAMPLES_AVCONV += $(FATE_LOCO)
fate-loco: $(FATE_LOCO)

FATE_SAMPLES_AVCONV += fate-msrle-8bit
fate-msrle-8bit: CMD = framecrc -i $(SAMPLES)/msrle/Search-RLE.avi -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-mszh
fate-mszh: CMD = framecrc -i $(SAMPLES)/lcl/mszh-1frame.avi

FATE_SAMPLES_AVCONV += fate-vble
fate-vble: CMD = framecrc -i $(SAMPLES)/vble/flowers-partial-2MB.avi

FATE_SAMPLES_AVCONV += fate-zlib
fate-zlib: CMD = framecrc -i $(SAMPLES)/lcl/zlib-1frame.avi

FATE_SAMPLES_AVCONV += fate-zerocodec
fate-zerocodec: CMD = framecrc -i $(SAMPLES)/zerocodec/sample-zeco.avi
