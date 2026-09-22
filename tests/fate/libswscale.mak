FATE_LIBSWSCALE += fate-sws-pixdesc-query
fate-sws-pixdesc-query: libswscale/tests/pixdesc_query$(EXESUF)
fate-sws-pixdesc-query: CMD = run libswscale/tests/pixdesc_query$(EXESUF)

FATE_LIBSWSCALE += fate-sws-floatimg-cmp
fate-sws-floatimg-cmp: libswscale/tests/floatimg_cmp$(EXESUF)
fate-sws-floatimg-cmp: CMD = run libswscale/tests/floatimg_cmp$(EXESUF)

FATE_LIBSWSCALE += fate-sws-rgb2rgb-bounds
fate-sws-rgb2rgb-bounds: libswscale/tests/colorspace$(EXESUF)
fate-sws-rgb2rgb-bounds: CMD = run libswscale/tests/colorspace$(EXESUF)
fate-sws-rgb2rgb-bounds: CMP = null

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

ifeq ($(CONFIG_UNSTABLE),yes)
SWS_UNSTABLE_BACKENDS-yes                            += c memcpy
ifeq ($(ARCH_X86_64),yes)
SWS_UNSTABLE_BACKENDS-$(HAVE_X86ASM)                 += x86
endif
ifeq ($(ARCH_AARCH64),yes)
SWS_UNSTABLE_BACKENDS-$(HAVE_NEON)                   += aarch64
endif
# TODO: enable spirv tests once they pass on all Vulkan devices.
# SWS_UNSTABLE_BACKENDS-$(HAVE_SPIRV_HEADERS_SPIRV_H)  += spirv
# SWS_UNSTABLE_BACKENDS-$(HAVE_SPIRV_UNIFIED1_SPIRV_H) += spirv

# Set -hw default for Vulkan tests.
fate-sws-unscaled-spirv fate-sws-unstable-spirv: SWS_HW = -hw default

# Legacy swscale fails this self-check; test each available ops backend.
FATE_SWS_UNSCALED := $(SWS_UNSTABLE_BACKENDS-yes:%=fate-sws-unscaled-%)
$(FATE_SWS_UNSCALED): libswscale/tests/swscale$(EXESUF)
$(FATE_SWS_UNSCALED): CMD = run libswscale/tests/swscale$(EXESUF) -v 16 $(SWS_HW) -scaler none -backends $(@:fate-sws-unscaled-%=%)
$(FATE_SWS_UNSCALED): REF = /dev/null
fate-sws-unscaled: $(FATE_SWS_UNSCALED)

# Run only 2% of swscale tests to keep the run time short, and only check for failure
FATE_SWS_UNSTABLE := $(SWS_UNSTABLE_BACKENDS-yes:%=fate-sws-unstable-%)
$(FATE_SWS_UNSTABLE): libswscale/tests/swscale$(EXESUF)
$(FATE_SWS_UNSTABLE): CMD = run libswscale/tests/swscale$(EXESUF) -v 16 $(SWS_HW) -backends $(@:fate-sws-unstable-%=%) -p 0.02
$(FATE_SWS_UNSTABLE): REF = /dev/null
fate-sws-unstable: $(FATE_SWS_UNSTABLE)

FATE_LIBSWSCALE += $(FATE_SWS_UNSCALED) $(FATE_SWS_UNSTABLE)
endif

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
fate-sws-uops-macros: libswscale/uops_macros_gen$(EXESUF)
fate-sws-uops-macros: REF = $(SRC_PATH)/libswscale/uops_macros.h
fate-sws-uops-macros: CMD = run libswscale/uops_macros_gen$(EXESUF)

endif

FATE_LIBSWSCALE-$(CONFIG_UNSTABLE) += fate-sws-rational64
fate-sws-rational64: libswscale/tests/rational64$(EXESUF)
fate-sws-rational64: CMD = run libswscale/tests/rational64$(EXESUF)
fate-sws-rational64: CMP = null

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
