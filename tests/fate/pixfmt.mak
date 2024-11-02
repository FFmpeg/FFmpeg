PIXFMT_8_LIST =         bgr24           \
                        gray            \
                        nv12            \
                        nv16            \
                        nv24            \
                        monob           \
                        monow           \
                        vuyx            \
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
                        yvyu422         \
                        uyvy422         \
                        vyu444          \

FATE_PIXFMT := $(PIXFMT_8_LIST:%=fate-pixfmt-%)

$(FATE_PIXFMT): CMD = pixfmt_conversion
$(FATE_PIXFMT): REF = $(SRC_PATH)/tests/ref/pixfmt/$(@:fate-pixfmt-%=%)
$(FATE_PIXFMT): $(VREF)

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

PIXFMT_EXT_LIST =      $(PIXFMT_8_LIST) \
                        gray10le        \
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
                        gray12le        \
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
                      $(PIXFMT_16_LIST) \


FATE_PIXFMT_8-YUV-$(call ALLYES, SCALE_FILTER YUVTESTSRC_FILTER LAVFI_INDEV) += $(PIXFMT_EXT_LIST)
FATE_PIXFMT_8-RGB-$(call ALLYES, SCALE_FILTER RGBTESTSRC_FILTER LAVFI_INDEV) += $(PIXFMT_EXT_LIST)

FATE_PIXFMT_8-YUV += $(FATE_PIXFMT_8-YUV-yes:%=fate-pixfmt-yuv444p-%)
FATE_PIXFMT_8-RGB += $(FATE_PIXFMT_8-RGB-yes:%=fate-pixfmt-gbrp-%)
FATE_PIXFMT_8-RGB += $(FATE_PIXFMT_8-RGB-yes:%=fate-pixfmt-rgb24-%)

$(FATE_PIXFMT_8-YUV): CMD = pixfmt_conversion_ext "yuv"
$(FATE_PIXFMT_8-RGB): CMD = pixfmt_conversion_ext "rgb"

FATE_PIXFMT_8 := $(FATE_PIXFMT_8-YUV) $(FATE_PIXFMT_8-RGB)
$(FATE_PIXFMT_8): REF = $(SRC_PATH)/tests/ref/pixfmt/$(@:fate-pixfmt-%=%)

FATE_PIXFMT_EXT-YUV-$(call ALLYES, SCALE_FILTER YUVTESTSRC_FILTER LAVFI_INDEV) += $(PIXFMT_EXT_LIST)
FATE_PIXFMT_EXT-RGB-$(call ALLYES, SCALE_FILTER RGBTESTSRC_FILTER LAVFI_INDEV) += $(PIXFMT_EXT_LIST)

FATE_PIXFMT_EXT-YUV += $(FATE_PIXFMT_EXT-YUV-yes:%=fate-pixfmt-yuv444p10-%)
FATE_PIXFMT_EXT-YUV += $(FATE_PIXFMT_EXT-YUV-yes:%=fate-pixfmt-yuv444p12-%)
FATE_PIXFMT_EXT-RGB += $(FATE_PIXFMT_EXT-RGB-yes:%=fate-pixfmt-gbrp10-%)
FATE_PIXFMT_EXT-RGB += $(FATE_PIXFMT_EXT-RGB-yes:%=fate-pixfmt-gbrp12-%)
FATE_PIXFMT_EXT-RGB += $(FATE_PIXFMT_EXT-RGB-yes:%=fate-pixfmt-rgb48-%)

$(FATE_PIXFMT_EXT-YUV): CMD = pixfmt_conversion_ext "yuv" "le"
$(FATE_PIXFMT_EXT-RGB): CMD = pixfmt_conversion_ext "rgb" "le"

FATE_PIXFMT_EXT := $(FATE_PIXFMT_EXT-YUV) $(FATE_PIXFMT_EXT-RGB)
$(FATE_PIXFMT_EXT): REF = $(SRC_PATH)/tests/ref/pixfmt/$(@:fate-pixfmt-%=%)

FATE_PIXFMT_16-YUV-$(call ALLYES, SCALE_FILTER YUVTESTSRC_FILTER LAVFI_INDEV) += $(PIXFMT_16_LIST)
FATE_PIXFMT_16-RGB-$(call ALLYES, SCALE_FILTER RGBTESTSRC_FILTER LAVFI_INDEV) += $(PIXFMT_16_LIST)

FATE_PIXFMT_16-YUV := $(FATE_PIXFMT_16-YUV-yes:%=fate-pixfmt-yuv444p16-%)
FATE_PIXFMT_16-RGB := $(FATE_PIXFMT_16-RGB-yes:%=fate-pixfmt-gbrp16-%)

$(FATE_PIXFMT_16-YUV): CMD = pixfmt_conversion_ext "yuv" "le"
$(FATE_PIXFMT_16-RGB): CMD = pixfmt_conversion_ext "rgb" "le"

FATE_PIXFMT_16 := $(FATE_PIXFMT_16-YUV) $(FATE_PIXFMT_16-RGB)
$(FATE_PIXFMT_16): REF = $(SRC_PATH)/tests/ref/pixfmt/$(@:fate-pixfmt-%=%)

FATE_AVCONV += $(FATE_PIXFMT) $(FATE_PIXFMT_8) $(FATE_PIXFMT_EXT) $(FATE_PIXFMT_16)
fate-pixfmt:   $(FATE_PIXFMT) $(FATE_PIXFMT_8) $(FATE_PIXFMT_EXT) $(FATE_PIXFMT_16)
