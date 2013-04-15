FATE_LAVFI = fate-lavfi-pixfmts_copy                                    \
             fate-lavfi-pixfmts_crop                                    \
             fate-lavfi-pixfmts_hflip                                   \
             fate-lavfi-pixfmts_null                                    \
             fate-lavfi-pixfmts_pad                                     \
             fate-lavfi-pixfmts_scale                                   \
             fate-lavfi-pixfmts_vflip                                   \

$(FATE_LAVFI): $(VREF) libavfilter/filtfmts-test$(EXESUF)
$(FATE_LAVFI): CMD = lavfitest

FATE_AVCONV += $(FATE_LAVFI)
fate-lavfi:    $(FATE_LAVFI)
