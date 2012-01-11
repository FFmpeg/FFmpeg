FATE_UTVIDEO += fate-utvideo_rgba_left
fate-utvideo_rgba_left: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgba_left.avi

FATE_UTVIDEO += fate-utvideo_rgba_median
fate-utvideo_rgba_median: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgba_median.avi

FATE_UTVIDEO += fate-utvideo_rgb_left
fate-utvideo_rgb_left: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgb_left.avi

FATE_UTVIDEO += fate-utvideo_rgb_median
fate-utvideo_rgb_median: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgb_median.avi

FATE_UTVIDEO += fate-utvideo_yuv420_left
fate-utvideo_yuv420_left: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_yuv420_left.avi

FATE_UTVIDEO += fate-utvideo_yuv420_median
fate-utvideo_yuv420_median: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_yuv420_median.avi

FATE_UTVIDEO += fate-utvideo_yuv422_left
fate-utvideo_yuv422_left: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_yuv422_left.avi

FATE_UTVIDEO += fate-utvideo_yuv422_median
fate-utvideo_yuv422_median: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_yuv422_median.avi

FATE_TESTS += $(FATE_UTVIDEO)
fate-utvideo: $(FATE_UTVIDEO)
