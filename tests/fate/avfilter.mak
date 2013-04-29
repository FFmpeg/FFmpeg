#FIXME the whole file should be removed

FATE_LAVFI = fate-lavfi-alphaextract_rgb                                \
             fate-lavfi-alphaextract_yuv                                \
             fate-lavfi-alphamerge_rgb                                  \
             fate-lavfi-alphamerge_yuv                                  \
             fate-lavfi-field                                           \
             fate-lavfi-il                                              \

FATE_LAVFI-$(CONFIG_AVDEVICE) += fate-lavfi-life                        \
                                 fate-lavfi-scalenorm                   \
                                 fate-lavfi-testsrc                     \

FATE_LAVFI-$(CONFIG_GPL) += fate-lavfi-colormatrix1                     \
                            fate-lavfi-colormatrix2                     \
                            fate-lavfi-kerndeint                        \
                            fate-lavfi-pixfmts_super2xsai               \
                            fate-lavfi-tinterlace_merge                 \
                            fate-lavfi-tinterlace_pad                   \

FATE_LAVFI += $(FATE_LAVFI-yes)

$(FATE_LAVFI): $(VREF) libavfilter/filtfmts-test$(EXESUF)
$(FATE_LAVFI): CMD = lavfitest

FATE_AVCONV += $(FATE_LAVFI)
fate-lavfi:    $(FATE_LAVFI)

