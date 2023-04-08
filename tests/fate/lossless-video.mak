FATE_LAGARITH += fate-lagarith-rgb24
fate-lagarith-rgb24: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lag-rgb24.avi

FATE_LAGARITH-$(call FRAMECRC, AVI, LAGARITH, SCALE_FILTER) += fate-lagarith-rgb32
fate-lagarith-rgb32: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lag-rgb32.avi -pix_fmt bgra -vf scale

FATE_LAGARITH += fate-lagarith-yuy2
fate-lagarith-yuy2: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lag-yuy2.avi

FATE_LAGARITH += fate-lagarith-yv12
fate-lagarith-yv12: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lag-yv12.avi

FATE_LAGARITH += fate-lagarith-red
fate-lagarith-red: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lagarith-red.avi

FATE_LAGARITH += fate-lagarith-ticket4119 fate-lagarith-ticket4119-cfr fate-lagarith-ticket4119-vfr fate-lagarith-ticket4119-pass
fate-lagarith-ticket4119: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lagarith-1.3.27-black-frames-and-off-by-ones.avi
fate-lagarith-ticket4119-cfr : CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lagarith-1.3.27-black-frames-and-off-by-ones.avi -fps_mode cfr
fate-lagarith-ticket4119-vfr : CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lagarith-1.3.27-black-frames-and-off-by-ones.avi -fps_mode vfr
fate-lagarith-ticket4119-pass: CMD = framecrc -i $(TARGET_SAMPLES)/lagarith/lagarith-1.3.27-black-frames-and-off-by-ones.avi -fps_mode passthrough

FATE_LAGARITH-$(call FRAMECRC, AVI, LAGARITH) += $(FATE_LAGARITH)

FATE_LOSSLESS_VIDEO += $(FATE_LAGARITH-yes)
fate-lagarith: $(FATE_LAGARITH-yes)

FATE_LOCO-$(call FRAMECRC, AVI, LOCO) += fate-loco-rgb fate-loco-yuy2
fate-loco-rgb:  CMD = framecrc -i $(TARGET_SAMPLES)/loco/pig-loco-rgb.avi
fate-loco-yuy2: CMD = framecrc -i $(TARGET_SAMPLES)/loco/pig-loco-0.avi

FATE_LOSSLESS_VIDEO += $(FATE_LOCO-yes)
fate-loco: $(FATE_LOCO-yes)

FATE_LOSSLESS_VIDEO-$(call FRAMECRC, AVI, MSRLE, SCALE_FILTER) += fate-msrle-8bit
fate-msrle-8bit: CMD = framecrc -i $(TARGET_SAMPLES)/msrle/Search-RLE.avi -pix_fmt rgb24 -vf scale

FATE_LOSSLESS_VIDEO-$(call FRAMECRC, AVI, MSZH) += fate-mszh
fate-mszh: CMD = framecrc -i $(TARGET_SAMPLES)/lcl/mszh-1frame.avi

FATE_LOSSLESS_VIDEO-$(call FRAMECRC, AVI, VBLE) += fate-vble
fate-vble: CMD = framecrc -i $(TARGET_SAMPLES)/vble/flowers-partial-2MB.avi

FATE_LOSSLESS_VIDEO-$(call FRAMECRC, AVI, ZEROCODEC) += fate-zerocodec
fate-zerocodec: CMD = framecrc -i $(TARGET_SAMPLES)/zerocodec/sample-zeco.avi

FATE_LOSSLESS_VIDEO-$(call FRAMECRC, AVI, ZLIB) += fate-zlib
fate-zlib: CMD = framecrc -i $(TARGET_SAMPLES)/lcl/zlib-1frame.avi

FATE_LOSSLESS_VIDEO += $(FATE_LOSSLESS_VIDEO-yes)

FATE_SAMPLES_FFMPEG += $(FATE_LOSSLESS_VIDEO)
fate-lossless-video: $(FATE_LOSSLESS_VIDEO)
