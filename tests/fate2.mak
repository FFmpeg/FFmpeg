FATE_TESTS += fate-mpeg2-field-enc
fate-mpeg2-field-enc: CMD = framecrc -flags +bitexact -dct fastint -idct simple -i $(SAMPLES)/mpeg2/mpeg2_field_encoding.ts -an

FATE_TESTS += fate-yop
fate-yop: CMD = framecrc -i $(SAMPLES)/yop/test1.yop -pix_fmt rgb24 -an

FATE_TESTS += fate-ansi
fate-ansi: CMD = framecrc -chars_per_frame 44100 -i $(SAMPLES)/ansi/TRE-IOM5.ANS -pix_fmt rgb24

FATE_TESTS += fate-txd-pal8
fate-txd-pal8: CMD = framecrc -i $(SAMPLES)/txd/outro.txd -pix_fmt rgb24 -an

FATE_TESTS += fate-txd-16bpp
fate-txd-16bpp: CMD = framecrc -i $(SAMPLES)/txd/misc.txd -pix_fmt bgra -an

FATE_TESTS += fate-dxa-scummvm
fate-dxa-scummvm: CMD = framecrc -i $(SAMPLES)/dxa/scummvm.dxa -pix_fmt rgb24

FATE_TESTS += fate-mjpegb
fate-mjpegb: CMD = framecrc -idct simple -flags +bitexact -i $(SAMPLES)/mjpegb/mjpegb_part.mov -an

FATE_TESTS += fate-v410dec
fate-v410dec: CMD = framecrc -i $(SAMPLES)/v410/lenav410.mov -pix_fmt yuv444p10le

FATE_TESTS += fate-v410enc
fate-v410enc: tests/vsynth1/00.pgm
fate-v410enc: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -vcodec v410 -f avi

FATE_TESTS += fate-r210
fate-r210: CMD = framecrc -i $(SAMPLES)/r210/r210.avi -pix_fmt rgb48le

FATE_TESTS += fate-xxan-wc4
fate-xxan-wc4: CMD = framecrc -i $(SAMPLES)/wc4-xan/wc4_2.avi -an -vframes 10
