VVC_SAMPLES_8BIT =        \
    CodingToolsSets_A_2   \

VVC_SAMPLES_10BIT =       \
    APSALF_A_2            \
    APSLMCS_D_1           \
    APSMULT_A_4           \
    AUD_A_3               \
    BUMP_A_2              \
    DCI_A_3               \
    HRD_A_3               \
    IBC_B_Tencent_2       \
    PHSH_B_1              \
    POC_A_1               \
    PPS_B_1               \
    RAP_A_1               \
    RPR_A_4               \
    SAO_A_3               \
    SCALING_A_1           \
    SLICES_A_3            \
    SPS_B_1               \
    STILL_B_1             \
    SUBPIC_A_3            \
    SUBPIC_C_ERICSSON_1   \
    TILE_A_2              \
    WP_A_3                \
    WPP_A_3               \
    WRAP_A_4              \

VVC_SAMPLES_444_10BIT =   \
    CROP_B_4              \

# not tested:
# BOUNDARY_A_3 (too big)
# OPI_B_3 (Inter layer ref support needed)
# VPS_A_3 (Inter layer ref support needed)

FATE_VVC_VARS := 8BIT 10BIT 444_10BIT
$(foreach VAR,$(FATE_VVC_VARS), $(eval VVC_TESTS_$(VAR) := $(addprefix fate-vvc-conformance-, $(VVC_SAMPLES_$(VAR)))))

$(VVC_TESTS_8BIT): SCALE_OPTS := -pix_fmt yuv420p
$(VVC_TESTS_10BIT): SCALE_OPTS := -pix_fmt yuv420p10le -vf scale
$(VVC_TESTS_444_10BIT): SCALE_OPTS := -pix_fmt yuv444p10le -vf scale
fate-vvc-conformance-%: CMD = framecrc -c:v vvc -i $(TARGET_SAMPLES)/vvc-conformance/$(subst fate-vvc-conformance-,,$(@)).bit $(SCALE_OPTS)

FATE_VVC-$(call FRAMECRC, VVC, VVC, VVC_PARSER) += $(VVC_TESTS_8BIT)
FATE_VVC-$(call FRAMECRC, VVC, VVC, VVC_PARSER SCALE_FILTER) +=            \
                                                    $(VVC_TESTS_10BIT)     \
                                                    $(VVC_TESTS_444_10BIT) \

FATE_SAMPLES_FFMPEG += $(FATE_VVC-yes)

fate-vvc: $(FATE_VVC-yes)
