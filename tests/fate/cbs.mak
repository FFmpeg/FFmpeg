# Read/write tests: By default, this uses the codec metadata filters - with no
# arguments, it decomposes the stream fully and then recomposes it
# without making any changes.

fate-cbs: fate-cbs-apv fate-cbs-av1 fate-cbs-h264 fate-cbs-hevc fate-cbs-mpeg2 fate-cbs-vp9 fate-cbs-vvc

FATE_CBS_DEPS = $(call ALLYES, $(1)_DEMUXER $(2)_PARSER $(3)_METADATA_BSF $(4)_DECODER $(5)_MUXER)
FATE_CBS_NO_DEC_DEPS = $(call ALLYES, $(1)_DEMUXER $(1)_PARSER $(1)_METADATA_BSF $(1)_MUXER)

define FATE_CBS_TEST
# (codec, test_name, sample_file, output_format)
FATE_CBS_$(1) += fate-cbs-$(1)-$(2)
fate-cbs-$(1)-$(2): CMD = md5 -c:v $(3) -i $(TARGET_SAMPLES)/$(4) -c:v copy -y -bsf:v $(1)_metadata -f $(5)
endef

define FATE_CBS_NO_DEC_TEST
# (codec, test_name, sample_file, output_format)
FATE_CBS_$(1) += fate-cbs-$(1)-$(2)
fate-cbs-$(1)-$(2): CMD = md5 -i $(TARGET_SAMPLES)/$(3) -c:v copy -y -bsf:v $(1)_metadata -f $(4)
endef

define FATE_CBS_DISCARD_TEST
# (codec, discard_type, sample_file, output_format)
FATE_CBS_$(1)_DISCARD += fate-cbs-$(1)-discard-$(2)
fate-cbs-$(1)-discard-$(2): CMD = md5 -i $(TARGET_SAMPLES)/$(3) -c:v copy -y -bsf:v filter_units=discard=$(2) -f $(4)
endef

# APV read/write

FATE_CBS_APV_SAMPLES =             \
    profile_422-10.apv

$(foreach N,$(FATE_CBS_APV_SAMPLES),$(eval $(call FATE_CBS_TEST,apv,$(basename $(N)),apv,apv/$(N),apv)))

FATE_CBS_APV-$(call FATE_CBS_DEPS, APV, APV, APV, APV, APV) = $(FATE_CBS_apv)
FATE_SAMPLES_AVCONV += $(FATE_CBS_APV-yes)
fate-cbs-apv: $(FATE_CBS_APV-yes)

# AV1 read/write

FATE_CBS_AV1_CONFORMANCE_SAMPLES = \
    av1-1-b8-02-allintra.ivf       \
    av1-1-b8-03-sizedown.ivf       \
    av1-1-b8-03-sizeup.ivf         \
    av1-1-b8-04-cdfupdate.ivf      \
    av1-1-b8-05-mv.ivf             \
    av1-1-b8-06-mfmv.ivf           \
    av1-1-b8-22-svc-L1T2.ivf       \
    av1-1-b8-22-svc-L2T1.ivf       \
    av1-1-b8-22-svc-L2T2.ivf       \
    av1-1-b8-23-film_grain-50.ivf  \
    av1-1-b10-23-film_grain-50.ivf

FATE_CBS_AV1_SAMPLES =              \
    decode_model.ivf                \
    frames_refs_short_signaling.ivf \
    non_uniform_tiling.ivf          \
    seq_hdr_op_param_info.ivf       \
    switch_frame.ivf

$(foreach N,$(FATE_CBS_AV1_CONFORMANCE_SAMPLES),$(eval $(call FATE_CBS_TEST,av1,$(basename $(N)),av1,av1-test-vectors/$(N),rawvideo)))
$(foreach N,$(FATE_CBS_AV1_SAMPLES),$(eval $(call FATE_CBS_TEST,av1,$(basename $(N)),av1,av1/$(N),rawvideo)))

FATE_CBS_AV1-$(call FATE_CBS_DEPS, IVF, AV1, AV1, AV1, RAWVIDEO) = $(FATE_CBS_av1)
FATE_SAMPLES_AVCONV += $(FATE_CBS_AV1-yes)
fate-cbs-av1: $(FATE_CBS_AV1-yes)

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

$(foreach N,$(FATE_CBS_H264_CONFORMANCE_SAMPLES),$(eval $(call FATE_CBS_TEST,h264,$(basename $(N)),h264,h264-conformance/$(N),h264)))
$(foreach N,$(FATE_CBS_H264_SAMPLES),$(eval $(call FATE_CBS_TEST,h264,$(basename $(N)),h264,h264/$(N),h264)))

FATE_CBS_H264-$(call FATE_CBS_DEPS, H264, H264, H264, H264, H264) = $(FATE_CBS_h264)

FATE_CBS_DISCARD_TYPES = \
    nonref   \
    bidir    \
    nonintra \
    nonkey

$(foreach N,$(FATE_CBS_DISCARD_TYPES),$(eval $(call FATE_CBS_DISCARD_TEST,h264,$(N),h264/interlaced_crop.mp4,h264)))

FATE_CBS_H264-$(call ALLYES, MOV_DEMUXER H264_MUXER H264_PARSER FILTER_UNITS_BSF H264_METADATA_BSF FILE_PROTOCOL) += $(FATE_CBS_h264_DISCARD)


FATE_H264_REDUNDANT_PPS-$(call REMUX, H264, MOV_DEMUXER H264_REDUNDANT_PPS_BSF   \
                                      H264_DECODER H264_PARSER RAWVIDEO_ENCODER) \
                                      += fate-h264_redundant_pps-mov
fate-h264_redundant_pps-mov: CMD = transcode \
        mov $(TARGET_SAMPLES)/mov/frag_overlap.mp4 h264 \
        "-map 0:v -c copy -bsf h264_redundant_pps"

# This file has changing pic_init_qp_minus26.
FATE_H264_REDUNDANT_PPS-$(call REMUX, H264, H264_PARSER H264_REDUNDANT_PPS_BSF \
                                      H264_DECODER RAWVIDEO_ENCODER) \
                                      += fate-h264_redundant_pps-annexb
fate-h264_redundant_pps-annexb: CMD = transcode \
        h264 $(TARGET_SAMPLES)/h264-conformance/CABA3_TOSHIBA_E.264 \
        h264 "-map 0:v -c copy -bsf h264_redundant_pps"

# These two tests test that new extradata in packet side data is properly
# modified by h264_redundant_pps. nut is used as destination container
# because it can store extradata updates (in its experimental mode);
# setting -syncpoints none is a hack to use nut version 4.
FATE_H264_REDUNDANT_PPS-$(call REMUX, NUT, MOV_DEMUXER H264_REDUNDANT_PPS_BSF H264_DECODER) \
                        += fate-h264_redundant_pps-side_data
fate-h264_redundant_pps-side_data: CMD = transcode \
        mov $(TARGET_SAMPLES)/h264/thezerotheorem-cut.mp4 nut \
        "-map 0:v -c copy -bsf h264_redundant_pps -syncpoints none -strict experimental" "-c copy"

FATE_H264_REDUNDANT_PPS-$(call REMUX, NUT, MOV_DEMUXER H264_REDUNDANT_PPS_BSF \
                                      H264_DECODER SCALE_FILTER RAWVIDEO_ENCODER) \
                                      += fate-h264_redundant_pps-side_data2
fate-h264_redundant_pps-side_data2: CMD = transcode \
        mov $(TARGET_SAMPLES)/h264/extradata-reload-multi-stsd.mov nut \
        "-map 0:v -c copy -bsf h264_redundant_pps -syncpoints none -strict experimental"

fate-h264_redundant_pps: $(FATE_H264_REDUNDANT_PPS-yes)


FATE_SAMPLES_FFMPEG += $(FATE_CBS_H264-yes) $(FATE_H264_REDUNDANT_PPS-yes)
fate-cbs-h264: $(FATE_CBS_H264-yes) $(FATE_H264_REDUNDANT_PPS-yes)

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

$(foreach N,$(FATE_CBS_HEVC_SAMPLES),$(eval $(call FATE_CBS_TEST,hevc,$(basename $(N)),hevc,hevc-conformance/$(N),hevc)))

FATE_CBS_HEVC-$(call FATE_CBS_DEPS, HEVC, HEVC, HEVC, HEVC, HEVC) = $(FATE_CBS_hevc)

$(foreach N,$(FATE_CBS_DISCARD_TYPES),$(eval $(call FATE_CBS_DISCARD_TEST,hevc,$(N),hevc-conformance/WPP_A_ericsson_MAIN10_2.bit,hevc)))

FATE_CBS_HEVC-$(call ALLYES, HEVC_DEMUXER HEVC_MUXER HEVC_PARSER FILTER_UNITS_BSF HEVC_METADATA_BSF FILE_PROTOCOL) += $(FATE_CBS_hevc_DISCARD)

fate-cbs-hevc-metadata-set-color: CMD = md5 -i $(TARGET_SAMPLES)/hevc-conformance/AMP_A_Samsung_4.bit -c:v copy -bsf:v hevc_metadata=colour_primaries=0:transfer_characteristics=0:matrix_coefficients=3 -f hevc
fate-cbs-hevc-metadata-set-color: CMP = oneline
fate-cbs-hevc-metadata-set-color: REF = d073124fca9e30a46c173292f948967c
FATE_CBS_HEVC-$(call ALLYES, HEVC_DEMUXER, HEVC_METADATA_BSF, HEVC_MUXER) += fate-cbs-hevc-metadata-set-color

FATE_SAMPLES_AVCONV += $(FATE_CBS_HEVC-yes)
fate-cbs-hevc: $(FATE_CBS_HEVC-yes)

# H.266 read/write

FATE_CBS_VVC_SAMPLES =        \
    APSALF_A_2.bit            \
    APSLMCS_D_1.bit           \
    APSMULT_A_4.bit           \
    AUD_A_3.bit               \
    BOUNDARY_A_3.bit          \
    BUMP_A_2.bit              \
    CodingToolsSets_A_2.bit   \
    CROP_B_4.bit              \
    DCI_A_3.bit               \
    HRD_A_3.bit               \
    OPI_B_3.bit               \
    PHSH_B_1.bit              \
    POC_A_1.bit               \
    PPS_B_1.bit               \
    RAP_A_1.bit               \
    SAO_A_3.bit               \
    SCALING_A_1.bit           \
    SLICES_A_3.bit            \
    SPS_B_1.bit               \
    STILL_B_1.bit             \
    SUBPIC_A_3.bit            \
    TILE_A_2.bit              \
    VPS_A_3.bit               \
    WP_A_3.bit                \
    WPP_A_3.bit               \
    WRAP_A_4.bit              \


$(foreach N,$(FATE_CBS_VVC_SAMPLES),$(eval $(call FATE_CBS_NO_DEC_TEST,vvc,$(basename $(N)),vvc-conformance/$(N),vvc)))

FATE_CBS_VVC-$(call FATE_CBS_NO_DEC_DEPS, VVC) = $(FATE_CBS_vvc)

FATE_SAMPLES_AVCONV += $(FATE_CBS_VVC-yes)
fate-cbs-vvc: $(FATE_CBS_VVC-yes)

# MPEG-2 read/write

FATE_CBS_MPEG2_SAMPLES =     \
    hhi_burst_422_short.bits \
    sony-ct3.bs              \
    tcela-6.bits

$(foreach N,$(FATE_CBS_MPEG2_SAMPLES),$(eval $(call FATE_CBS_TEST,mpeg2,$(basename $(N)),mpeg2video,mpeg2/$(N),mpeg2video)))

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

$(foreach N,$(FATE_CBS_VP9_SAMPLES),$(eval $(call FATE_CBS_TEST,vp9,$(basename $(N)),vp9,vp9-test-vectors/$(N),ivf)))

FATE_CBS_VP9-$(call FATE_CBS_DEPS, IVF, VP9, VP9, VP9, IVF) = $(FATE_CBS_vp9)
FATE_SAMPLES_AVCONV += $(FATE_CBS_VP9-yes)
fate-cbs-vp9: $(FATE_CBS_VP9-yes)
