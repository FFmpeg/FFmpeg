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

FATE_AVCONV += $(FATE_PIXFMT)
fate-pixfmt:   $(FATE_PIXFMT)
