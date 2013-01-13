FATE_UTVIDEO += fate-utvideo_rgb_left
fate-utvideo_rgb_left: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgb_left.avi

FATE_UTVIDEO += fate-utvideo_rgb_median
fate-utvideo_rgb_median: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgb_median.avi

FATE_UTVIDEO += fate-utvideo_rgba_left
fate-utvideo_rgba_left: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgba_left.avi

FATE_UTVIDEO += fate-utvideo_rgba_median
fate-utvideo_rgba_median: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgba_median.avi

FATE_UTVIDEO += fate-utvideo_rgba_single_symbol
fate-utvideo_rgba_single_symbol: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgba_single_symbol.avi

FATE_UTVIDEO += fate-utvideo_yuv420_left
fate-utvideo_yuv420_left: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_yuv420_left.avi

FATE_UTVIDEO += fate-utvideo_yuv420_median
fate-utvideo_yuv420_median: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_yuv420_median.avi

FATE_UTVIDEO += fate-utvideo_yuv422_left
fate-utvideo_yuv422_left: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_yuv422_left.avi

FATE_UTVIDEO += fate-utvideo_yuv422_median
fate-utvideo_yuv422_median: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_yuv422_median.avi

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, UTVIDEO) += $(FATE_UTVIDEO)
fate-utvideo: $(FATE_UTVIDEO)

fate-utvideoenc%: CMD = framemd5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -vcodec utvideo -sws_flags +accurate_rnd+bitexact ${OPTS}

FATE_UTVIDEOENC += fate-utvideoenc_rgba_left
fate-utvideoenc_rgba_left: OPTS = -pix_fmt rgba -pred left

FATE_UTVIDEOENC += fate-utvideoenc_rgba_median
fate-utvideoenc_rgba_median: OPTS = -pix_fmt rgba -pred median

FATE_UTVIDEOENC += fate-utvideoenc_rgba_none
fate-utvideoenc_rgba_none: OPTS = -pix_fmt rgba -pred 3

FATE_UTVIDEOENC += fate-utvideoenc_rgb_left
fate-utvideoenc_rgb_left: OPTS = -pix_fmt rgb24 -pred left

FATE_UTVIDEOENC += fate-utvideoenc_rgb_median
fate-utvideoenc_rgb_median: OPTS = -pix_fmt rgb24 -pred median

FATE_UTVIDEOENC += fate-utvideoenc_rgb_none
fate-utvideoenc_rgb_none: OPTS = -pix_fmt rgb24 -pred 3

FATE_UTVIDEOENC += fate-utvideoenc_yuv420_left
fate-utvideoenc_yuv420_left: OPTS = -pix_fmt yuv420p -pred left

FATE_UTVIDEOENC += fate-utvideoenc_yuv420_median
fate-utvideoenc_yuv420_median: OPTS = -pix_fmt yuv420p -pred median

FATE_UTVIDEOENC += fate-utvideoenc_yuv420_none
fate-utvideoenc_yuv420_none: OPTS = -pix_fmt yuv420p -pred 3

FATE_UTVIDEOENC += fate-utvideoenc_yuv422_left
fate-utvideoenc_yuv422_left: OPTS = -pix_fmt yuv422p -pred left

FATE_UTVIDEOENC += fate-utvideoenc_yuv422_median
fate-utvideoenc_yuv422_median: OPTS = -pix_fmt yuv422p -pred median

FATE_UTVIDEOENC += fate-utvideoenc_yuv422_none
fate-utvideoenc_yuv422_none: OPTS = -pix_fmt yuv422p -pred 3

$(FATE_UTVIDEOENC): tests/vsynth1/00.pgm

FATE_AVCONV-$(call ENCMUX, UTVIDEO, AVI) += $(FATE_UTVIDEOENC)
fate-utvideoenc: $(FATE_UTVIDEOENC)
