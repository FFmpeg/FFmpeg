tests/data/fits-multi.fits: TAG = GEN
tests/data/fits-multi.fits: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< \
        -i $(TARGET_SAMPLES)/gif/m4nb.gif \
        -y $(TARGET_PATH)/$(@) 2>/dev/null

#mapping of fits file formats to png filenames
map.tests/data/lena-gray.fits    := gray8
map.tests/data/lena-gbrp.fits    := rgb24
map.tests/data/lena-gbrp16.fits  := rgb48
map.tests/data/lena-gbrap16le.fits := rgba64

tests/data/lena%.fits: TAG = GEN
tests/data/lena%.fits: NAME = $(map.$(@))
tests/data/lena%.fits: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< \
        -i $(TARGET_SAMPLES)/png1/lena-$(map.$(@)).png \
        -y $(TARGET_PATH)/$(@) 2>/dev/null

FATE_FITS_DEC-$(call DEMDEC, FITS, FITS) += fate-fitsdec-ext_data_min_max
fate-fitsdec-ext_data_min_max: CMD = framecrc -i $(TARGET_SAMPLES)/fits/x0cj010ct_d0h.fit -pix_fmt gray16le -vf scale

FATE_FITS_DEC-$(call DEMDEC, FITS, FITS) += fate-fitsdec-blank_bitpix32
fate-fitsdec-blank_bitpix32: CMD = framecrc -blank_value 65535 -i $(TARGET_SAMPLES)/fits/file008.fits -pix_fmt gray16le -vf scale

FATE_FITS_DEC-$(call DEMDEC, FITS, FITS) += fate-fitsdec-bitpix-32
fate-fitsdec-bitpix-32: CMD = framecrc -i $(TARGET_SAMPLES)/fits/tst0005.fits -pix_fmt gray16le -vf scale

FATE_FITS_DEC-$(call DEMDEC, FITS, FITS) += fate-fitsdec-bitpix-64
fate-fitsdec-bitpix-64: CMD = framecrc -i $(TARGET_SAMPLES)/fits/tst0006.fits -pix_fmt gray16le -vf scale

FATE_FITS_DEC-$(call ALLYES, GIF_DEMUXER FITS_DEMUXER GIF_DECODER FITS_DECODER FITS_ENCODER FITS_MUXER) += fate-fitsdec-multi
fate-fitsdec-multi: tests/data/fits-multi.fits
fate-fitsdec-multi: CMD = framecrc -i $(TARGET_PATH)/tests/data/fits-multi.fits -pix_fmt gbrap

fate-fitsdec%: PIXFMT = $(word 3, $(subst -, ,$(@)))
fate-fitsdec%: SRC = $(TARGET_PATH)/tests/data/lena-$(PIXFMT).fits
fate-fitsdec%: CMD = framecrc -i $(SRC) -pix_fmt $(PIXFMT)

FATE_FITS_DEC_PIXFMT = gray gbrp gbrp16 gbrap16le
$(FATE_FITS_DEC_PIXFMT:%=fate-fitsdec-%): fate-fitsdec-%: tests/data/lena-%.fits
FATE_FITS_DEC-$(call ALLYES, FITS_DEMUXER IMAGE2_DEMUXER FITS_DECODER PNG_DECODER FITS_ENCODER FITS_MUXER) += $(FATE_FITS_DEC_PIXFMT:%=fate-fitsdec-%)

FATE_FITS += $(FATE_FITS_DEC-yes)
fate-fitsdec: $(FATE_FITS_DEC-yes)

fate-fitsenc%: PIXFMT = $(word 3, $(subst -, ,$(@)))
fate-fitsenc%: SRC = $(TARGET_PATH)/tests/data/fits-multi.fits
fate-fitsenc%: CMD = framecrc -auto_conversion_filters -i $(SRC) -c:v fits -pix_fmt $(PIXFMT)

FATE_FITS_ENC_PIXFMT = gray gray16be gbrp gbrap gbrp16be gbrap16be
$(FATE_FITS_ENC_PIXFMT:%=fate-fitsenc-%): tests/data/fits-multi.fits
FATE_FITS_ENC-$(call ALLYES, GIF_DEMUXER GIF_DECODER FITS_ENCODER FITS_MUXER) += $(FATE_FITS_ENC_PIXFMT:%=fate-fitsenc-%)

FATE_FITS += $(FATE_FITS_ENC-yes)
fate-fitsenc: $(FATE_FITS_ENC-yes)

FATE_SAMPLES_FFMPEG += $(FATE_FITS)
fate-fits: $(FATE_FITS)
