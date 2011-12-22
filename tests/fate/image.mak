FATE_IMAGE += fate-dpx
fate-dpx: CMD = framecrc  -i $(SAMPLES)/dpx/lighthouse_rgb48.dpx

FATE_IMAGE += fate-fax-g3
fate-fax-g3: CMD = framecrc -i $(SAMPLES)/CCITT_fax/G31D.TIF

FATE_IMAGE += fate-fax-g3s
fate-fax-g3s: CMD = framecrc -i $(SAMPLES)/CCITT_fax/G31DS.TIF

FATE_IMAGE += fate-pictor
fate-pictor: CMD = framecrc -i $(SAMPLES)/pictor/MFISH.PIC -pix_fmt rgb24

FATE_IMAGE += fate-ptx
fate-ptx: CMD = framecrc  -i $(SAMPLES)/ptx/_113kw_pic.ptx -pix_fmt rgb24

FATE_IMAGE += fate-sunraster-1bit-raw
fate-sunraster-1bit-raw: CMD = framecrc  -i $(SAMPLES)/sunraster/lena-1bit-raw.sun

FATE_IMAGE += fate-sunraster-1bit-rle
fate-sunraster-1bit-rle: CMD = framecrc  -i $(SAMPLES)/sunraster/lena-1bit-rle.sun

FATE_IMAGE += fate-sunraster-8bit-raw
fate-sunraster-8bit-raw: CMD = framecrc  -i $(SAMPLES)/sunraster/lena-8bit-raw.sun -pix_fmt rgb24

FATE_IMAGE += fate-sunraster-8bit-rle
fate-sunraster-8bit-rle: CMD = framecrc  -i $(SAMPLES)/sunraster/lena-8bit-rle.sun -pix_fmt rgb24

FATE_IMAGE += fate-sunraster-24bit-raw
fate-sunraster-24bit-raw: CMD = framecrc  -i $(SAMPLES)/sunraster/lena-24bit-raw.sun

FATE_IMAGE += fate-sunraster-24bit-rle
fate-sunraster-24bit-rle: CMD = framecrc  -i $(SAMPLES)/sunraster/lena-24bit-rle.sun

FATE_TESTS += $(FATE_IMAGE)
fate-image: $(FATE_IMAGE)
