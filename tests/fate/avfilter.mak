#FIXME the whole file should be removed

FATE_LAVFI = fate-lavfi-alphaextract_rgb                                \
             fate-lavfi-alphaextract_yuv                                \
             fate-lavfi-alphamerge_rgb                                  \
             fate-lavfi-alphamerge_yuv                                  \
             fate-lavfi-drawbox                                         \
             fate-lavfi-edgedetect                                      \
             fate-lavfi-fade                                            \
             fate-lavfi-field                                           \
             fate-lavfi-idet                                            \
             fate-lavfi-il                                              \
             fate-lavfi-overlay_rgb                                     \
             fate-lavfi-overlay_yuv420                                  \
             fate-lavfi-overlay_yuv444                                  \
             fate-lavfi-pad                                             \
             fate-lavfi-select                                          \
             fate-lavfi-setdar                                          \
             fate-lavfi-setsar                                          \
             fate-lavfi-thumbnail                                       \
             fate-lavfi-tile                                            \
             fate-lavfi-transpose                                       \
             fate-lavfi-unsharp                                         \

FATE_LAVFI-$(CONFIG_AVDEVICE) += fate-lavfi-life                        \
                                 fate-lavfi-scalenorm                   \
                                 fate-lavfi-testsrc                     \

FATE_LAVFI-$(CONFIG_GPL) += fate-lavfi-colormatrix1                     \
                            fate-lavfi-colormatrix2                     \
                            fate-lavfi-hue                              \
                            fate-lavfi-kerndeint                        \
                            fate-lavfi-pixfmts_super2xsai               \
                            fate-lavfi-pp                               \
                            fate-lavfi-pp2                              \
                            fate-lavfi-pp3                              \
                            fate-lavfi-pp4                              \
                            fate-lavfi-pp5                              \
                            fate-lavfi-pp6                              \
                            fate-lavfi-tinterlace_merge                 \
                            fate-lavfi-tinterlace_pad                   \

FATE_LAVFI += $(FATE_LAVFI-yes)

$(FATE_LAVFI): $(VREF) libavfilter/filtfmts-test$(EXESUF)
$(FATE_LAVFI): CMD = lavfitest

FATE_AVCONV += $(FATE_LAVFI)
fate-lavfi:    $(FATE_LAVFI)

