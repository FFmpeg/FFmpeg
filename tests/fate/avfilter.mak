FATE_LAVFI = fate-lavfi-crop                                            \
             fate-lavfi-crop_scale                                      \
             fate-lavfi-crop_scale_vflip                                \
             fate-lavfi-crop_vflip                                      \
             fate-lavfi-null                                            \
             fate-lavfi-pixdesc                                         \
             fate-lavfi-pixfmts_copy                                    \
             fate-lavfi-pixfmts_crop                                    \
             fate-lavfi-pixfmts_hflip                                   \
             fate-lavfi-pixfmts_null                                    \
             fate-lavfi-pixfmts_pad                                     \
             fate-lavfi-pixfmts_scale                                   \
             fate-lavfi-pixfmts_vflip                                   \
             fate-lavfi-scale200                                        \
             fate-lavfi-scale500                                        \
             fate-lavfi-vflip                                           \
             fate-lavfi-vflip_crop                                      \
             fate-lavfi-vflip_vflip                                     \

$(FATE_LAVFI): $(VREF) libavfilter/filtfmts-test$(EXESUF)
$(FATE_LAVFI): CMD = lavfitest

FATE_AVCONV += $(FATE_LAVFI)
fate-lavfi:    $(FATE_LAVFI)
