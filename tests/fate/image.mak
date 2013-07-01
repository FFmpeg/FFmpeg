FATE_IMAGE-$(call DEMDEC, IMAGE2, DPX) += fate-dpx
fate-dpx: CMD = framecrc -i $(TARGET_SAMPLES)/dpx/lighthouse_rgb48.dpx

FATE_EXR += fate-exr-slice-raw
fate-exr-slice-raw: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_slice_raw.exr -pix_fmt rgba64le

FATE_EXR += fate-exr-slice-rle
fate-exr-slice-rle: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_slice_rle.exr -pix_fmt rgba64le

FATE_EXR += fate-exr-slice-zip1
fate-exr-slice-zip1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_slice_zip1.exr -pix_fmt rgba64le

FATE_EXR += fate-exr-slice-zip16
fate-exr-slice-zip16: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_slice_zip16.exr -pix_fmt rgba64le

FATE_EXR-$(call DEMDEC, IMAGE2, EXR) += $(FATE_EXR)

FATE_IMAGE += $(FATE_EXR-yes)
fate-exr: $(FATE_EXR-yes)

FATE_IMAGE-$(call DEMDEC, IMAGE2, PICTOR) += fate-pictor
fate-pictor: CMD = framecrc -i $(TARGET_SAMPLES)/pictor/MFISH.PIC -pix_fmt rgb24

FATE_IMAGE-$(call DEMDEC, IMAGE2, PTX) += fate-ptx
fate-ptx: CMD = framecrc -i $(TARGET_SAMPLES)/ptx/_113kw_pic.ptx -pix_fmt rgb24

FATE_SUNRASTER += fate-sunraster-1bit-raw
fate-sunraster-1bit-raw: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/lena-1bit-raw.sun

FATE_SUNRASTER += fate-sunraster-1bit-rle
fate-sunraster-1bit-rle: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/lena-1bit-rle.sun

FATE_SUNRASTER += fate-sunraster-8bit-raw
fate-sunraster-8bit-raw: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/lena-8bit-raw.sun -pix_fmt rgb24

FATE_SUNRASTER += fate-sunraster-8bit_gray-raw
fate-sunraster-8bit_gray-raw: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/gray.ras

FATE_SUNRASTER += fate-sunraster-8bit-rle
fate-sunraster-8bit-rle: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/lena-8bit-rle.sun -pix_fmt rgb24

FATE_SUNRASTER += fate-sunraster-24bit-raw
fate-sunraster-24bit-raw: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/lena-24bit-raw.sun

FATE_SUNRASTER += fate-sunraster-24bit-rle
fate-sunraster-24bit-rle: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/lena-24bit-rle.sun

FATE_SUNRASTER-$(call DEMDEC, IMAGE2, SUNRAST) += $(FATE_SUNRASTER)

FATE_IMAGE += $(FATE_SUNRASTER-yes)
fate-sunraster: $(FATE_SUNRASTER-yes)

FATE_TARGA = CBW8       \
             CCM8       \
             CTC16      \
             CTC24      \
             CTC32      \
             UBW8       \
             UCM8       \
             UTC16      \
             UTC24      \
             UTC32

FATE_TARGA := $(FATE_TARGA:%=fate-targa-conformance-%)  \
              fate-targa-top-to-bottom

FATE_TARGA-$(call DEMDEC, IMAGE2, TARGA) += $(FATE_TARGA)

FATE_IMAGE += $(FATE_TARGA-yes)
fate-targa: $(FATE_TARGA-yes)

fate-targa-conformance-CBW8:  CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/CBW8.TGA
fate-targa-conformance-CCM8:  CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/CCM8.TGA  -pix_fmt rgba
fate-targa-conformance-CTC16: CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/CTC16.TGA -pix_fmt rgb555le
fate-targa-conformance-CTC24: CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/CTC24.TGA
fate-targa-conformance-CTC32: CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/CTC32.TGA -pix_fmt bgra
fate-targa-conformance-UBW8:  CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/UBW8.TGA
fate-targa-conformance-UCM8:  CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/UCM8.TGA  -pix_fmt rgba
fate-targa-conformance-UTC16: CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/UTC16.TGA -pix_fmt rgb555le
fate-targa-conformance-UTC24: CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/UTC24.TGA
fate-targa-conformance-UTC32: CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/UTC32.TGA -pix_fmt bgra

fate-targa-top-to-bottom: CMD = framecrc -i $(TARGET_SAMPLES)/targa/lena-top-to-bottom.tga

FATE_TIFF += fate-tiff-fax-g3
fate-tiff-fax-g3: CMD = framecrc -i $(TARGET_SAMPLES)/CCITT_fax/G31D.TIF

FATE_TIFF += fate-tiff-fax-g3s
fate-tiff-fax-g3s: CMD = framecrc -i $(TARGET_SAMPLES)/CCITT_fax/G31DS.TIF

FATE_TIFF-$(call DEMDEC, IMAGE2, TIFF) += $(FATE_TIFF)

FATE_IMAGE += $(FATE_TIFF-yes)
fate-tiff: $(FATE_TIFF-yes)

FATE_IMAGE-$(call DEMDEC, IMAGE2, XFACE) += fate-xface
fate-xface: CMD = framecrc -i $(TARGET_SAMPLES)/xface/lena.xface

FATE_IMAGE += $(FATE_IMAGE-yes)

FATE_SAMPLES_FFMPEG += $(FATE_IMAGE)
fate-image: $(FATE_IMAGE)
