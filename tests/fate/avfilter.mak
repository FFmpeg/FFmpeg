FATE_LAVFI = fate-lavfi-alphaextract_rgb                                \
             fate-lavfi-alphaextract_yuv                                \
             fate-lavfi-alphamerge_rgb                                  \
             fate-lavfi-alphamerge_yuv                                  \
             fate-lavfi-crop                                            \
             fate-lavfi-crop_scale                                      \
             fate-lavfi-crop_scale_vflip                                \
             fate-lavfi-crop_vflip                                      \
             fate-lavfi-drawbox                                         \
             fate-lavfi-edgedetect                                      \
             fate-lavfi-fade                                            \
             fate-lavfi-field                                           \
             fate-lavfi-idet                                            \
             fate-lavfi-il                                              \
             fate-lavfi-life                                            \
             fate-lavfi-null                                            \
             fate-lavfi-overlay                                         \
             fate-lavfi-pad                                             \
             fate-lavfi-pixfmts_copy                                    \
             fate-lavfi-pixfmts_crop                                    \
             fate-lavfi-pixfmts_hflip                                   \
             fate-lavfi-pixfmts_null                                    \
             fate-lavfi-pixfmts_pad                                     \
             fate-lavfi-pixfmts_pixdesctest                             \
             fate-lavfi-pixfmts_scale                                   \
             fate-lavfi-pixfmts_vflip                                   \
             fate-lavfi-scale200                                        \
             fate-lavfi-scale500                                        \
             fate-lavfi-scalenorm                                       \
             fate-lavfi-select                                          \
             fate-lavfi-setdar                                          \
             fate-lavfi-setsar                                          \
             fate-lavfi-testsrc                                         \
             fate-lavfi-thumbnail                                       \
             fate-lavfi-tile                                            \
             fate-lavfi-transpose                                       \
             fate-lavfi-unsharp                                         \
             fate-lavfi-vflip                                           \
             fate-lavfi-vflip_crop                                      \
             fate-lavfi-vflip_vflip                                     \

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

