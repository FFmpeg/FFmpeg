FATE_BMP += fate-bmp-1bit
fate-bmp-1bit: CMD = framecrc -i $(SAMPLES)/bmp/test1.bmp -pix_fmt rgb24

FATE_BMP += fate-bmp-4bit
fate-bmp-4bit: CMD = framecrc -i $(SAMPLES)/bmp/test4.bmp -pix_fmt rgb24

FATE_BMP += fate-bmp-4bit-os2
fate-bmp-4bit-os2: CMD = framecrc -i $(SAMPLES)/bmp/test4os2v2.bmp -pix_fmt rgb24

FATE_BMP += fate-bmp-8bit
fate-bmp-8bit: CMD = framecrc -i $(SAMPLES)/bmp/test8.bmp -pix_fmt rgb24

FATE_BMP += fate-bmp-8bit-os2
fate-bmp-8bit-os2: CMD = framecrc -i $(SAMPLES)/bmp/test8os2.bmp -pix_fmt rgb24

FATE_BMP += fate-bmp-15bit
fate-bmp-15bit: CMD = framecrc -i $(SAMPLES)/bmp/test16.bmp -pix_fmt rgb555le

FATE_BMP += fate-bmp-15bit-mask
fate-bmp-15bit-mask: CMD = framecrc -i $(SAMPLES)/bmp/test16bf555.bmp -pix_fmt rgb555le

FATE_BMP += fate-bmp-16bit-mask
fate-bmp-16bit-mask: CMD = framecrc -i $(SAMPLES)/bmp/test16bf565.bmp -pix_fmt rgb565le

FATE_BMP += fate-bmp-24bit
fate-bmp-24bit: CMD = framecrc -i $(SAMPLES)/bmp/test24.bmp

FATE_BMP += fate-bmp-32bit
fate-bmp-32bit: CMD = framecrc -i $(SAMPLES)/bmp/test32.bmp -pix_fmt bgr24

FATE_BMP += fate-bmp-32bit-mask
fate-bmp-32bit-mask: CMD = framecrc -i $(SAMPLES)/bmp/test32bf.bmp -pix_fmt bgr24

FATE_BMP += fate-bmp-rle4
fate-bmp-rle4: CMD = framecrc -i $(SAMPLES)/bmp/testcompress4.bmp -pix_fmt rgb24

FATE_BMP += fate-bmp-rle8
fate-bmp-rle8: CMD = framecrc -i $(SAMPLES)/bmp/testcompress8.bmp -pix_fmt rgb24

FATE_SAMPLES_AVCONV += $(FATE_BMP)
fate-bmp: $(FATE_BMP)
