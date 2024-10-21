FATE_PIXFMT-$(CONFIG_SCALE_FILTER) =           bgr24           \
                        gray            \
                        monob           \
                        monow           \
                        rgb24           \
                        rgb32           \
                        rgb555          \
                        rgb565          \
                        xyz12le         \
                        yuv410p         \
                        yuv411p         \
                        yuv420p         \
                        yuv422p         \
                        yuv440p         \
                        yuv444p         \
                        yuvj420p        \
                        yuvj422p        \
                        yuvj440p        \
                        yuvj444p        \
                        yuyv422         \

FATE_PIXFMT := $(FATE_PIXFMT-yes:%=fate-pixfmt-%)

$(FATE_PIXFMT): CMD = pixfmt_conversion
$(FATE_PIXFMT): REF = $(SRC_PATH)/tests/ref/pixfmt/$(@:fate-pixfmt-%=%)
$(FATE_PIXFMT): $(VREF)

PIXFMT_10_LIST =        gray10le        \
                        gray10be        \
                        yuv420p10le     \
                        yuv420p10be     \
                        yuv422p10le     \
                        yuv422p10be     \
                        yuv440p10le     \
                        yuv440p10be     \
                        yuv444p10le     \
                        yuv444p10be     \
                        y210le          \
                        p010le          \
                        p010be          \
                        p210le          \
                        p210be          \
                        p410le          \
                        p410be          \
                        v30xle          \
                        xv30le          \
                        x2rgb10le       \
                        x2bgr10le       \
                        gbrp10le        \
                        gbrp10be        \

FATE_PIXFMT_10-YUV-$(call ALLYES, SCALE_FILTER YUVTESTSRC_FILTER LAVFI_INDEV) += $(PIXFMT_10_LIST)
FATE_PIXFMT_10-RGB-$(call ALLYES, SCALE_FILTER RGBTESTSRC_FILTER LAVFI_INDEV) += $(PIXFMT_10_LIST)

FATE_PIXFMT_10-YUV := $(FATE_PIXFMT_10-YUV-yes:%=fate-pixfmt-yuv444p10-%)
FATE_PIXFMT_10-RGB := $(FATE_PIXFMT_10-RGB-yes:%=fate-pixfmt-gbrp10-%)

$(FATE_PIXFMT_10-YUV): CMD = pixfmt_conversion_ext "yuv"
$(FATE_PIXFMT_10-RGB): CMD = pixfmt_conversion_ext "rgb"

FATE_PIXFMT_10 := $(FATE_PIXFMT_10-YUV) $(FATE_PIXFMT_10-RGB)
$(FATE_PIXFMT_10): REF = $(SRC_PATH)/tests/ref/pixfmt/$(@:fate-pixfmt-%=%)

PIXFMT_12_LIST =        gray12le        \
                        gray12be        \
                        yuv420p12le     \
                        yuv420p12be     \
                        yuv422p12le     \
                        yuv422p12be     \
                        yuv440p12le     \
                        yuv440p12be     \
                        yuv444p12le     \
                        yuv444p12be     \
                        y212le          \
                        p012le          \
                        p012be          \
                        p212le          \
                        p212be          \
                        p412le          \
                        p412be          \
                        xv36le          \
                        xv36be          \
                        gbrp12le        \
                        gbrp12be        \

FATE_PIXFMT_12-YUV-$(call ALLYES, SCALE_FILTER YUVTESTSRC_FILTER LAVFI_INDEV) += $(PIXFMT_12_LIST)
FATE_PIXFMT_12-RGB-$(call ALLYES, SCALE_FILTER RGBTESTSRC_FILTER LAVFI_INDEV) += $(PIXFMT_12_LIST)

FATE_PIXFMT_12-YUV := $(FATE_PIXFMT_12-YUV-yes:%=fate-pixfmt-yuv444p12-%)
FATE_PIXFMT_12-RGB := $(FATE_PIXFMT_12-RGB-yes:%=fate-pixfmt-gbrp12-%)

$(FATE_PIXFMT_12-YUV): CMD = pixfmt_conversion_ext "yuv"
$(FATE_PIXFMT_12-RGB): CMD = pixfmt_conversion_ext "rgb"

FATE_PIXFMT_12 := $(FATE_PIXFMT_12-YUV) $(FATE_PIXFMT_12-RGB)
$(FATE_PIXFMT_12): REF = $(SRC_PATH)/tests/ref/pixfmt/$(@:fate-pixfmt-%=%)

PIXFMT_16_LIST =        gray16le        \
                        gray16be        \
                        yuv420p16le     \
                        yuv420p16be     \
                        yuv422p16le     \
                        yuv422p16be     \
                        yuv444p16le     \
                        yuv444p16be     \
                        y216le          \
                        p016le          \
                        p016be          \
                        p216le          \
                        p216be          \
                        p416le          \
                        p416be          \
                        xv48le          \
                        xv48be          \
                        rgb48           \
                        gbrp16le        \
                        gbrp16be        \

FATE_PIXFMT_16-YUV-$(call ALLYES, SCALE_FILTER YUVTESTSRC_FILTER LAVFI_INDEV) += $(PIXFMT_16_LIST)
FATE_PIXFMT_16-RGB-$(call ALLYES, SCALE_FILTER RGBTESTSRC_FILTER LAVFI_INDEV) += $(PIXFMT_16_LIST)

FATE_PIXFMT_16-YUV := $(FATE_PIXFMT_16-YUV-yes:%=fate-pixfmt-yuv444p16-%)
FATE_PIXFMT_16-RGB := $(FATE_PIXFMT_16-RGB-yes:%=fate-pixfmt-gbrp16-%)

$(FATE_PIXFMT_16-YUV): CMD = pixfmt_conversion_ext "yuv"
$(FATE_PIXFMT_16-RGB): CMD = pixfmt_conversion_ext "rgb"

FATE_PIXFMT_16 := $(FATE_PIXFMT_16-YUV) $(FATE_PIXFMT_16-RGB)
$(FATE_PIXFMT_16): REF = $(SRC_PATH)/tests/ref/pixfmt/$(@:fate-pixfmt-%=%)

FATE_AVCONV += $(FATE_PIXFMT) $(FATE_PIXFMT_8) $(FATE_PIXFMT_10) $(FATE_PIXFMT_12) $(FATE_PIXFMT_16)
fate-pixfmt:   $(FATE_PIXFMT) $(FATE_PIXFMT_8) $(FATE_PIXFMT_10) $(FATE_PIXFMT_12) $(FATE_PIXFMT_16)
