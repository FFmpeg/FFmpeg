FATE_UTVIDEO += fate-utvideo_rgb_left
fate-utvideo_rgb_left: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_rgb_left.avi

FATE_UTVIDEO += fate-utvideo_rgb_median
fate-utvideo_rgb_median: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_rgb_median.avi

FATE_UTVIDEO += fate-utvideo_rgba_left
fate-utvideo_rgba_left: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_rgba_left.avi

FATE_UTVIDEO += fate-utvideo_rgba_median
fate-utvideo_rgba_median: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_rgba_median.avi

FATE_UTVIDEO += fate-utvideo_rgb_int_median
fate-utvideo_rgb_int_median: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_rgb_64x48_int_median.avi

FATE_UTVIDEO += fate-utvideo_rgba_gradient
fate-utvideo_rgba_gradient: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_rgba_gradient.avi

FATE_UTVIDEO += fate-utvideo_rgb_int_gradient
fate-utvideo_rgb_int_gradient: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_rgb_64x48_int_gradient.avi

FATE_UTVIDEO += fate-utvideo_rgba_single_symbol
fate-utvideo_rgba_single_symbol: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_rgba_single_symbol.avi

FATE_UTVIDEO += fate-utvideo_yuv420_left
fate-utvideo_yuv420_left: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv420_left.avi

FATE_UTVIDEO += fate-utvideo_yuv420_median
fate-utvideo_yuv420_median: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv420_median.avi

FATE_UTVIDEO += fate-utvideo_yuv420_int_median
fate-utvideo_yuv420_int_median: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv420_709_64x48_int_median.avi

FATE_UTVIDEO += fate-utvideo_yuv420_gradient
fate-utvideo_yuv420_gradient: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv420_709_64x48_gradient.avi

FATE_UTVIDEO += fate-utvideo_yuv420_int_gradient
fate-utvideo_yuv420_int_gradient: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv420_709_64x48_int_gradient.avi

FATE_UTVIDEO += fate-utvideo_yuv422_left
fate-utvideo_yuv422_left: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv422_left.avi

FATE_UTVIDEO += fate-utvideo_yuv422_median
fate-utvideo_yuv422_median: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv422_median.avi

FATE_UTVIDEO += fate-utvideo_yuv422_int_median
fate-utvideo_yuv422_int_median: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv422_709_64x48_int_median.avi

FATE_UTVIDEO += fate-utvideo_yuv422_gradient
fate-utvideo_yuv422_gradient: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv422_709_64x48_gradient.avi

FATE_UTVIDEO += fate-utvideo_yuv422_int_gradient
fate-utvideo_yuv422_int_gradient: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv422_709_64x48_int_gradient.avi

FATE_UTVIDEO += fate-utvideo_yuv444_709_median
fate-utvideo_yuv444_709_median: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv444_709_64x48_median.avi

FATE_UTVIDEO += fate-utvideo_yuv444_709_int_median
fate-utvideo_yuv444_709_int_median: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv444_709_64x48_int_median.avi

FATE_UTVIDEO += fate-utvideo_yuv444_709_gradient
fate-utvideo_yuv444_709_gradient: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv444_709_gradient.avi

FATE_UTVIDEO += fate-utvideo_yuv444_709_int_gradient
fate-utvideo_yuv444_709_int_gradient: CMD = framecrc -i $(TARGET_SAMPLES)/utvideo/utvideo_yuv444_709_64x48_int_gradient.avi

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, UTVIDEO) += $(FATE_UTVIDEO)
fate-utvideo: $(FATE_UTVIDEO)

fate-utvideoenc%: CMD = framemd5 -f image2 -c:v pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -c:v utvideo -slices 1 -sws_flags +accurate_rnd+bitexact ${OPTS}

FATE_UTVIDEOENC += fate-utvideoenc_rgba_left
fate-utvideoenc_rgba_left: OPTS = -pix_fmt gbrap -pred left

FATE_UTVIDEOENC += fate-utvideoenc_rgba_median
fate-utvideoenc_rgba_median: OPTS = -pix_fmt gbrap -pred median

FATE_UTVIDEOENC += fate-utvideoenc_rgba_none
fate-utvideoenc_rgba_none: OPTS = -pix_fmt gbrap -pred none

FATE_UTVIDEOENC += fate-utvideoenc_rgb_left
fate-utvideoenc_rgb_left: OPTS = -pix_fmt gbrp -pred left

FATE_UTVIDEOENC += fate-utvideoenc_rgb_median
fate-utvideoenc_rgb_median: OPTS = -pix_fmt gbrp -pred median

FATE_UTVIDEOENC += fate-utvideoenc_rgb_none
fate-utvideoenc_rgb_none: OPTS = -pix_fmt gbrp -pred none

FATE_UTVIDEOENC += fate-utvideoenc_yuv420_left
fate-utvideoenc_yuv420_left: OPTS = -pix_fmt yuv420p -pred left

FATE_UTVIDEOENC += fate-utvideoenc_yuv420_median
fate-utvideoenc_yuv420_median: OPTS = -pix_fmt yuv420p -pred median

FATE_UTVIDEOENC += fate-utvideoenc_yuv420_none
fate-utvideoenc_yuv420_none: OPTS = -pix_fmt yuv420p -pred none

FATE_UTVIDEOENC += fate-utvideoenc_yuv422_left
fate-utvideoenc_yuv422_left: OPTS = -pix_fmt yuv422p -pred left

FATE_UTVIDEOENC += fate-utvideoenc_yuv422_median
fate-utvideoenc_yuv422_median: OPTS = -pix_fmt yuv422p -pred median

FATE_UTVIDEOENC += fate-utvideoenc_yuv422_none
fate-utvideoenc_yuv422_none: OPTS = -pix_fmt yuv422p -pred none

FATE_UTVIDEOENC += fate-utvideoenc_yuv444_left
fate-utvideoenc_yuv444_left: OPTS = -pix_fmt yuv444p -pred left

FATE_UTVIDEOENC += fate-utvideoenc_yuv444_median
fate-utvideoenc_yuv444_median: OPTS = -pix_fmt yuv444p -pred median

FATE_UTVIDEOENC += fate-utvideoenc_yuv444_none
fate-utvideoenc_yuv444_none: OPTS = -pix_fmt yuv444p -pred none

$(FATE_UTVIDEOENC): $(VREF)

FATE_AVCONV-$(call ENCMUX, UTVIDEO, AVI) += $(FATE_UTVIDEOENC)
fate-utvideoenc: $(FATE_UTVIDEOENC)
