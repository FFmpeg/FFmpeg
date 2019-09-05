# Read/write tests: this uses the codec metadata filter - with no
# arguments, it decomposes the stream fully and then recomposes it
# without making any changes.

fate-cbs: fate-cbs-h264 fate-cbs-hevc fate-cbs-mpeg2 fate-cbs-vp9

FATE_CBS_DEPS = $(call ALLYES, $(1)_DEMUXER $(2)_PARSER $(3)_METADATA_BSF $(4)_DECODER $(5)_MUXER)

define FATE_CBS_TEST
# (codec, test_name, sample_file, output_format)
FATE_CBS_$(1) += fate-cbs-$(1)-$(2)
fate-cbs-$(1)-$(2): CMD = md5 -i $(TARGET_SAMPLES)/$(3) -c:v copy -y -bsf:v $(1)_metadata -f $(4)
endef

# H.264 read/write

FATE_CBS_H264_CONFORMANCE_SAMPLES = \
    SVA_Base_B.264        \
    BASQP1_Sony_C.jsv     \
    FM1_BT_B.h264         \
    CVFC1_Sony_C.jsv      \
    AUD_MW_E.264          \
    CVBS3_Sony_C.jsv      \
    MR1_BT_A.h264         \
    CVWP1_TOSHIBA_E.264   \
    CVNLFI1_Sony_C.jsv    \
    Sharp_MP_PAFF_1r2.jvt \
    CVMANL1_TOSHIBA_B.264 \
    sp1_bt_a.h264         \
    CVSE2_Sony_B.jsv      \
    CABACI3_Sony_B.jsv

FATE_CBS_H264_SAMPLES = \
    sei-1.h264

$(foreach N,$(FATE_CBS_H264_CONFORMANCE_SAMPLES),$(eval $(call FATE_CBS_TEST,h264,$(basename $(N)),h264-conformance/$(N),h264)))
$(foreach N,$(FATE_CBS_H264_SAMPLES),$(eval $(call FATE_CBS_TEST,h264,$(basename $(N)),h264/$(N),h264)))

FATE_CBS_H264-$(call FATE_CBS_DEPS, H264, H264, H264, H264, H264) = $(FATE_CBS_h264)
FATE_SAMPLES_AVCONV += $(FATE_CBS_H264-yes)
fate-cbs-h264: $(FATE_CBS_H264-yes)

# H.265 read/write

FATE_CBS_HEVC_SAMPLES =       \
    STRUCT_A_Samsung_5.bit    \
    WP_A_Toshiba_3.bit        \
    SLIST_A_Sony_4.bit        \
    SLIST_D_Sony_9.bit        \
    CAINIT_E_SHARP_3.bit      \
    CAINIT_H_SHARP_3.bit      \
    TILES_B_Cisco_1.bit       \
    WPP_A_ericsson_MAIN_2.bit \
    WPP_F_ericsson_MAIN_2.bit \
    ipcm_E_NEC_2.bit          \
    NUT_A_ericsson_5.bit      \
    PICSIZE_A_Bossen_1.bit    \
    PICSIZE_B_Bossen_1.bit    \
    RPS_A_docomo_4.bit        \
    RPS_E_qualcomm_5.bit      \
    LTRPSPS_A_Qualcomm_1.bit  \
    RPLM_A_qualcomm_4.bit     \
    CONFWIN_A_Sony_1.bit      \
    HRD_A_Fujitsu_2.bit       \
    SLPPLP_A_VIDYO_2.bit

$(foreach N,$(FATE_CBS_HEVC_SAMPLES),$(eval $(call FATE_CBS_TEST,hevc,$(basename $(N)),hevc-conformance/$(N),hevc)))

FATE_CBS_HEVC-$(call FATE_CBS_DEPS, HEVC, HEVC, HEVC, HEVC, HEVC) = $(FATE_CBS_hevc)
FATE_SAMPLES_AVCONV += $(FATE_CBS_HEVC-yes)
fate-cbs-hevc: $(FATE_CBS_HEVC-yes)

# MPEG-2 read/write

FATE_CBS_MPEG2_SAMPLES =     \
    hhi_burst_422_short.bits \
    sony-ct3.bs              \
    tcela-6.bits

$(foreach N,$(FATE_CBS_MPEG2_SAMPLES),$(eval $(call FATE_CBS_TEST,mpeg2,$(basename $(N)),mpeg2/$(N),mpeg2video)))

FATE_CBS_MPEG2-$(call FATE_CBS_DEPS, MPEGVIDEO, MPEGVIDEO, MPEG2, MPEG2VIDEO, MPEG2VIDEO) = $(FATE_CBS_mpeg2)
FATE_SAMPLES_AVCONV += $(FATE_CBS_MPEG2-yes)
fate-cbs-mpeg2: $(FATE_CBS_MPEG2-yes)

# VP9 read/write

FATE_CBS_VP9_SAMPLES =                  \
    vp90-2-03-deltaq.webm               \
    vp90-2-05-resize.ivf                \
    vp90-2-06-bilinear.webm             \
    vp90-2-09-lf_deltas.webm            \
    vp90-2-10-show-existing-frame.webm  \
    vp90-2-10-show-existing-frame2.webm \
    vp90-2-segmentation-aq-akiyo.webm   \
    vp90-2-segmentation-sf-akiyo.webm   \
    vp90-2-tiling-pedestrian.webm       \
    vp91-2-04-yuv440.webm               \
    vp91-2-04-yuv444.webm               \
    vp92-2-20-10bit-yuv420.webm         \
    vp93-2-20-10bit-yuv422.webm         \
    vp93-2-20-12bit-yuv444.webm

$(foreach N,$(FATE_CBS_VP9_SAMPLES),$(eval $(call FATE_CBS_TEST,vp9,$(basename $(N)),vp9-test-vectors/$(N),ivf)))

FATE_CBS_VP9-$(call FATE_CBS_DEPS, IVF, VP9, VP9, VP9, IVF) = $(FATE_CBS_vp9)
FATE_SAMPLES_AVCONV += $(FATE_CBS_VP9-yes)
fate-cbs-vp9: $(FATE_CBS_VP9-yes)
