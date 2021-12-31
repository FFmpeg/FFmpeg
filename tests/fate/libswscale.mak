FATE_LIBSWSCALE += fate-sws-pixdesc-query
fate-sws-pixdesc-query: libswscale/tests/pixdesc_query$(EXESUF)
fate-sws-pixdesc-query: CMD = run libswscale/tests/pixdesc_query$(EXESUF)

FATE_LIBSWSCALE += fate-sws-floatimg-cmp
fate-sws-floatimg-cmp: libswscale/tests/floatimg_cmp$(EXESUF)
fate-sws-floatimg-cmp: CMD = run libswscale/tests/floatimg_cmp$(EXESUF)

SWS_SLICE_TEST-$(call DEMDEC, MATROSKA, VP9) += fate-sws-slice-yuv422-12bit-rgb48
fate-sws-slice-yuv422-12bit-rgb48: CMD = run tools/scale_slice_test$(EXESUF) $(TARGET_SAMPLES)/vp9-test-vectors/vp93-2-20-12bit-yuv422.webm 150 100 rgb48

SWS_SLICE_TEST-$(call DEMDEC, IMAGE_BMP_PIPE, BMP) += fate-sws-slice-bgr0-nv12
fate-sws-slice-bgr0-nv12: CMD = run tools/scale_slice_test$(EXESUF) $(TARGET_SAMPLES)/bmp/test32bf.bmp 32 64 nv12

fate-sws-slice: $(SWS_SLICE_TEST-yes)
$(SWS_SLICE_TEST-yes): tools/scale_slice_test$(EXESUF)
$(SWS_SLICE_TEST-yes): REF = /dev/null
FATE_LIBSWSCALE_SAMPLES += $(SWS_SLICE_TEST-yes)

FATE_LIBSWSCALE += $(FATE_LIBSWSCALE-yes)
FATE_LIBSWSCALE_SAMPLES += $(FATE_LIBSWSCALE_SAMPLES-yes)
FATE-$(CONFIG_SWSCALE) += $(FATE_LIBSWSCALE)
FATE_EXTERN-$(CONFIG_SWSCALE) += $(FATE_LIBSWSCALE_SAMPLES)
fate-libswscale: $(FATE_LIBSWSCALE) $(FATE_LIBSWSCALE_SAMPLES)
