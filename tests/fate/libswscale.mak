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

FATE_LIBSWSCALE_FFMPEG-$(call FRAMECRC, RAWVIDEO, RAWVIDEO, SCALE_FILTER) += fate-sws-yuv-colorspace \
                                                                             fate-sws-yuv-range
fate-sws-yuv-colorspace: tests/data/vsynth1.yuv
fate-sws-yuv-colorspace: CMD = framecrc \
  -f rawvideo -s 352x288 -pix_fmt yuv420p -i $(TARGET_PATH)/tests/data/vsynth1.yuv \
  -frames 1 \
  -vf scale=in_color_matrix=bt709:in_range=limited:out_color_matrix=bt601:out_range=full:flags=+accurate_rnd+bitexact

fate-sws-yuv-range: tests/data/vsynth1.yuv
fate-sws-yuv-range: CMD = framecrc \
  -f rawvideo -s 352x288 -pix_fmt yuv420p -i $(TARGET_PATH)/tests/data/vsynth1.yuv \
  -frames 1 \
  -vf scale=in_color_matrix=bt601:in_range=limited:out_color_matrix=bt601:out_range=full:flags=+accurate_rnd+bitexact

# This self-check currently fails for legacy swscale, so pass SWS_UNSTABLE to use the new code
FATE_LIBSWSCALE-$(CONFIG_UNSTABLE) += fate-sws-unscaled
fate-sws-unscaled: libswscale/tests/swscale$(EXESUF)
fate-sws-unscaled: CMD = run libswscale/tests/swscale$(EXESUF) -scaler none -backends unstable -v 16

# Run only 2% of swscale tests to keep the run time short, and only check for failure
FATE_LIBSWSCALE-$(CONFIG_UNSTABLE) += fate-sws-unstable
fate-sws-unstable: libswscale/tests/swscale$(EXESUF)
fate-sws-unstable: CMD = run libswscale/tests/swscale$(EXESUF) -backends unstable -p 0.02 -v 16

ifneq ($(HAVE_BIGENDIAN),yes)

# Disable on big endian because big endian platforms generate different op
# lists for le vs be formats; this breaks the checksum otherwise
FATE_LIBSWSCALE-$(CONFIG_UNSTABLE) += fate-sws-ops-list
fate-sws-ops-list: libswscale/tests/sws_ops$(EXESUF)
fate-sws-ops-list: CMD = run libswscale/tests/sws_ops$(EXESUF) | do_md5sum | cut -d" " -f1

ifeq ($(HAVE_INT128),yes)
# Disable by default without int128 because it is too slow (several minutes)
FATE_LIBSWSCALE-$(CONFIG_UNSTABLE) += fate-sws-uops-macros
endif

# Disable on bigendian because it would result in a different iteration order
# (and thus output) due to sorting by memcmp() on the parameters struct.
fate-sws-uops-macros: libswscale/tests/sws_ops$(EXESUF)
fate-sws-uops-macros: REF = $(SRC_PATH)/libswscale/uops_macros.h
fate-sws-uops-macros: CMD = run libswscale/tests/sws_ops$(EXESUF) -macros

endif

FATE_LIBSWSCALE-$(CONFIG_UNSTABLE) += fate-sws-ops-entries-aarch64
fate-sws-ops-entries-aarch64: libswscale/tests/sws_ops_aarch64$(EXESUF)
fate-sws-ops-entries-aarch64: REF = $(SRC_PATH)/libswscale/aarch64/ops_entries.c
fate-sws-ops-entries-aarch64: CMD = run libswscale/tests/sws_ops_aarch64$(EXESUF)

FATE_LIBSWSCALE += $(FATE_LIBSWSCALE-yes)
FATE_LIBSWSCALE_SAMPLES += $(FATE_LIBSWSCALE_SAMPLES-yes)
FATE-$(CONFIG_SWSCALE) += $(FATE_LIBSWSCALE)
FATE_FFMPEG += $(FATE_LIBSWSCALE_FFMPEG-yes)
FATE_EXTERN-$(CONFIG_SWSCALE) += $(FATE_LIBSWSCALE_SAMPLES)
fate-libswscale: $(FATE_LIBSWSCALE) $(FATE_LIBSWSCALE_SAMPLES) $(FATE_LIBSWSCALE_FFMPEG-yes)
