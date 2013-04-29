#FIXME the whole file should be removed

FATE_LAVFI-$(CONFIG_GPL) += fate-lavfi-tinterlace_merge                 \
                            fate-lavfi-tinterlace_pad                   \

FATE_LAVFI += $(FATE_LAVFI-yes)

$(FATE_LAVFI): $(VREF) libavfilter/filtfmts-test$(EXESUF)
$(FATE_LAVFI): CMD = lavfitest

FATE_AVCONV += $(FATE_LAVFI)
fate-lavfi:    $(FATE_LAVFI)

