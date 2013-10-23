# those samples have the checksums embedded, so the decoder itself can test
# correctness
FATE_HEVC_SEI =                 \
    CAINIT_A_SHARP_4            \
    CAINIT_B_SHARP_4            \
    CAINIT_C_SHARP_3            \
    CAINIT_D_SHARP_3            \
    CAINIT_E_SHARP_3            \
    CAINIT_F_SHARP_3            \
    CAINIT_G_SHARP_3            \
    CAINIT_H_SHARP_3            \
    CIP_A_Panasonic_3           \
    cip_B_NEC_2                 \
    CIP_C_Panasonic_2           \
    DSLICE_A_HHI_5              \
    DSLICE_B_HHI_5              \
    DSLICE_C_HHI_5              \
    ENTP_A_LG_2                 \
    ENTP_B_LG_2                 \
    ENTP_C_LG_3                 \
    EXT_A_ericsson_3            \
    ipcm_A_NEC_2                \
    ipcm_B_NEC_2                \
    ipcm_C_NEC_2                \
    ipcm_D_NEC_2                \
    IPRED_A_docomo_2            \
    IPRED_B_Nokia_3             \
    IPRED_C_Mitsubishi_2        \
    LS_A_Orange_2               \
    LS_B_ORANGE_3               \
    MAXBINS_A_TI_4              \
    MAXBINS_B_TI_4              \
    MAXBINS_C_TI_4              \
    MERGE_A_TI_3                \
    MERGE_B_TI_3                \
    MERGE_C_TI_3                \
    MERGE_D_TI_3                \
    MERGE_E_TI_3                \
    MERGE_G_HHI_4               \
    MVCLIP_A_qualcomm_3         \
    MVDL1ZERO_A_docomo_3        \
    MVEDGE_A_qualcomm_3         \
    NUT_A_ericsson_4            \
    PMERGE_A_TI_3               \
    PMERGE_B_TI_3               \
    PMERGE_C_TI_3               \
    PMERGE_D_TI_3               \
    PMERGE_E_TI_3               \
    PPS_A_qualcomm_7            \
    PS_A_VIDYO_3                \
    PS_B_VIDYO_3                \
    RAP_B_Bossen_1              \
    RPLM_A_qualcomm_4           \
    RPLM_B_qualcomm_4           \
    RPS_B_qualcomm_5            \
    RPS_C_ericsson_4            \
    RPS_D_ericsson_5            \
    RPS_E_qualcomm_5            \
    RQT_A_HHI_4                 \
    RQT_B_HHI_4                 \
    RQT_C_HHI_4                 \
    RQT_D_HHI_4                 \
    RQT_E_HHI_4                 \
    RQT_F_HHI_4                 \
    RQT_G_HHI_4                 \
    SAO_A_MediaTek_4            \
    SAO_B_MediaTek_5            \
    SDH_A_Orange_3              \
    SLICES_A_Rovi_3             \
    SLIST_A_Sony_4              \
    SLIST_B_Sony_8              \
    SLIST_C_Sony_3              \
    SLIST_D_Sony_9              \
    TSCL_A_VIDYO_5              \
    TSCL_B_VIDYO_4              \
    TSKIP_A_MS_2                \
    WP_A_Toshiba_3              \
    WP_B_Toshiba_3              \
    WP_A_MAIN10_Toshiba_3       \
    WP_MAIN10_B_Toshiba_3       \
    WPP_A_ericsson_MAIN_2       \
    WPP_B_ericsson_MAIN_2       \
    WPP_C_ericsson_MAIN_2       \
    WPP_D_ericsson_MAIN_2       \
    WPP_E_ericsson_MAIN_2       \
    WPP_F_ericsson_MAIN_2       \
    WPP_A_ericsson_MAIN10_2     \
    WPP_B_ericsson_MAIN10_2     \
    WPP_C_ericsson_MAIN10_2     \
    WPP_D_ericsson_MAIN10_2     \
    WPP_E_ericsson_MAIN10_2     \
    WPP_F_ericsson_MAIN10_2     \

# do not pass:
# DELTAQP_A_BRCM_4.bit -- TODO uses CRC instead of MD5
# HRD_A_Fujitsu_2.bin -- TODO uses hash 2 ("checksum")
# TSUNEQBD_A_MAIN10_Technicolor_2.bit (segfault)

define FATE_HEVC_SEI_TEST
FATE_HEVC += fate-hevc-conformance-$(1)
fate-hevc-conformance-$(1): CMD = ffmpeg -strict -2 -err_detect +explode -xerror -i $(TARGET_SAMPLES)/hevc-conformance/$(1).bit -f null -
fate-hevc-conformance-$(1): CMP = null
fate-hevc-conformance-$(1): REF = /dev/null
endef

$(foreach N,$(FATE_HEVC_SEI),$(eval $(call FATE_HEVC_SEI_TEST,$(N))))

# those samples don't have embedded checksums, so we test them with framecrc
FATE_HEVC_FRAMECRC =            \
    AMP_A_Samsung_4             \
    AMP_B_Samsung_4             \
    AMVP_C_Samsung_4            \
    AMP_D_Hisilicon             \
    AMP_E_Hisilicon             \
    AMP_F_Hisilicon_3           \
    AMVP_A_MTK_4                \
    AMVP_B_MTK_4                \
    DBLK_A_SONY_3               \
    DBLK_B_SONY_3               \
    DBLK_C_SONY_3               \
    DBLK_D_VIXS_1               \
    DBLK_D_VIXS_2               \
    DBLK_E_VIXS_1               \
    DBLK_E_VIXS_2               \
    DBLK_F_VIXS_1               \
    DBLK_F_VIXS_2               \
    DBLK_G_VIXS_1               \
    DBLK_G_VIXS_2               \
    DELTAQP_B_SONY_3            \
    DELTAQP_C_SONY_3            \
    MERGE_F_MTK_4               \
    PICSIZE_A_Bossen_1          \
    PICSIZE_B_Bossen_1          \
    PICSIZE_C_Bossen_1          \
    PICSIZE_D_Bossen_1          \
    POC_A_Bossen_3              \
    RAP_A_docomo_4              \
    RPS_A_docomo_4              \
    SAO_C_Samsung_4             \
    SAO_D_Samsung_4             \
    SAO_E_Canon_4               \
    SAO_F_Canon_3               \
    SAO_G_Canon_3               \
    STRUCT_A_Samsung_5          \
    STRUCT_B_Samsung_4          \
    TILES_A_Cisco_2             \
    TILES_B_Cisco_1             \
    TMVP_A_MS_2                 \

define FATE_HEVC_FRAMECRC_TEST
FATE_HEVC += fate-hevc-conformance-$(1)
fate-hevc-conformance-$(1): CMD = framecrc -strict -2 -vsync drop -i $(TARGET_SAMPLES)/hevc-conformance/$(1).bit
endef

$(foreach N,$(FATE_HEVC_FRAMECRC),$(eval $(call FATE_HEVC_FRAMECRC_TEST,$(N))))

FATE_HEVC += fate-hevc-conformance-DBLK_A_MAIN10_VIXS_2
fate-hevc-conformance-DBLK_A_MAIN10_VIXS_2: CMD = framecrc -strict -2 -vsync drop -i $(TARGET_SAMPLES)/hevc-conformance/DBLK_A_MAIN10_VIXS_2.bit -pix_fmt yuv420p10le

FATE_HEVC-$(call DEMDEC, HEVC, HEVC) += $(FATE_HEVC)

FATE_SAMPLES_AVCONV += $(FATE_HEVC-yes)

fate-hevc: $(FATE_HEVC-yes)
