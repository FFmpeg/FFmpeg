FATE_QTRLE += fate-qtrle-1bit
fate-qtrle-1bit: CMD = framecrc -i $(TARGET_SAMPLES)/qtrle/Animation-Monochrome.mov -an

FATE_QTRLE += fate-qtrle-2bit
fate-qtrle-2bit: CMD = framecrc -i $(TARGET_SAMPLES)/qtrle/Animation-4Greys.mov -pix_fmt rgb24 -an

FATE_QTRLE += fate-qtrle-4bit
fate-qtrle-4bit: CMD = framecrc -i $(TARGET_SAMPLES)/qtrle/Animation-16Greys.mov -pix_fmt rgb24 -an

FATE_QTRLE += fate-qtrle-8bit
fate-qtrle-8bit: CMD = framecrc -i $(TARGET_SAMPLES)/qtrle/criticalpath-credits.mov -pix_fmt rgb24 -an

FATE_QTRLE += fate-qtrle-16bit
fate-qtrle-16bit: CMD = framecrc -i $(TARGET_SAMPLES)/qtrle/mr-cork-rle.mov -pix_fmt rgb24

FATE_QTRLE += fate-qtrle-24bit
fate-qtrle-24bit: CMD = framecrc -i $(TARGET_SAMPLES)/qtrle/aletrek-rle.mov

FATE_QTRLE += fate-qtrle-32bit
fate-qtrle-32bit: CMD = framecrc -i $(TARGET_SAMPLES)/qtrle/ultra_demo_720_480_32bpp_rle.mov -pix_fmt bgra

FATE_SAMPLES_AVCONV-$(call DEMDEC, MOV, QTRLE) += $(FATE_QTRLE)
fate-qtrle: $(FATE_QTRLE)
