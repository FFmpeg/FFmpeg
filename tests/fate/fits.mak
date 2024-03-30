tests/data/fits-multi.fits: TAG = GEN
tests/data/fits-multi.fits: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -i $(TARGET_SAMPLES)/gif/m4nb.gif \
        -y $(TARGET_PATH)/$(@) 2>/dev/null

#mapping of fits file formats to png filenames
# TODO: Use an actual 64bit input file and fix the gbrp16 test on big-endian
fits-png-map-gray      := gray8
fits-png-map-gbrp      := rgb24
fits-png-map-gbrp16be  := rgb48
fits-png-map-gbrap16be := rgba64

FATE_FITS_DEC-$(call FRAMECRC, FITS, FITS, SCALE_FILTER) += fate-fitsdec-ext_data_min_max
fate-fitsdec-ext_data_min_max: CMD = framecrc -i $(TARGET_SAMPLES)/fits/x0cj010ct_d0h.fit -pix_fmt gray16le -vf scale

FATE_FITS_DEC-$(call FRAMECRC, FITS, FITS, SCALE_FILTER) += fate-fitsdec-blank_bitpix32
fate-fitsdec-blank_bitpix32: CMD = framecrc -blank_value 65535 -i $(TARGET_SAMPLES)/fits/file008.fits -pix_fmt gray16le -vf scale

FATE_FITS_DEC-$(call FRAMECRC, FITS, FITS, SCALE_FILTER) += fate-fitsdec-bitpix-32
fate-fitsdec-bitpix-32: CMD = framecrc -i $(TARGET_SAMPLES)/fits/tst0005.fits -pix_fmt gray16le -vf scale

FATE_FITS_DEC-$(call FRAMECRC, FITS, FITS, SCALE_FILTER) += fate-fitsdec-bitpix-64
fate-fitsdec-bitpix-64: CMD = framecrc -i $(TARGET_SAMPLES)/fits/tst0006.fits -pix_fmt gray16le -vf scale

FATE_FITS_DEC-$(call TRANSCODE, FITS, FITS, GIF_DEMUXER GIF_DECODER SCALE_FILTER) += fate-fitsdec-multi
fate-fitsdec-multi: tests/data/fits-multi.fits
fate-fitsdec-multi: CMD = framecrc -i $(TARGET_PATH)/tests/data/fits-multi.fits -pix_fmt gbrap

fate-fitsdec%: PIXFMT = $(word 3, $(subst -, ,$(@)))
fate-fitsdec%: CMD = transcode image2 $(TARGET_SAMPLES)/png1/lena-$(fits-png-map-$(PIXFMT)).png fits "-vf scale -pix_fmt $(PIXFMT)" "-vf scale -pix_fmt $(PIXFMT)"

FATE_FITS_DEC_PIXFMT = gray gbrp gbrp16be gbrap16be
FATE_FITS_DEC-$(call TRANSCODE, FITS, FITS, IMAGE2_DEMUXER PNG_DECODER SCALE_FILTER) += $(FATE_FITS_DEC_PIXFMT:%=fate-fitsdec-%)

FATE_FITS += $(FATE_FITS_DEC-yes)
fate-fitsdec: $(FATE_FITS_DEC-yes)

fate-fitsenc%: PIXFMT = $(word 3, $(subst -, ,$(@)))
fate-fitsenc%: SRC = $(TARGET_PATH)/tests/data/fits-multi.fits
fate-fitsenc%: CMD = framecrc -auto_conversion_filters -i $(SRC) -c:v fits -pix_fmt $(PIXFMT)

FATE_FITS_ENC_PIXFMT = gray gray16be gbrp gbrap gbrp16be gbrap16be
$(FATE_FITS_ENC_PIXFMT:%=fate-fitsenc-%): tests/data/fits-multi.fits
FATE_FITS_ENC-$(call TRANSCODE, FITS, FITS, GIF_DEMUXER GIF_DECODER SCALE_FILTER) += $(FATE_FITS_ENC_PIXFMT:%=fate-fitsenc-%)

FATE_FITS += $(FATE_FITS_ENC-yes)
fate-fitsenc: $(FATE_FITS_ENC-yes)

FATE_SAMPLES_FFMPEG += $(FATE_FITS)
fate-fits: $(FATE_FITS)
