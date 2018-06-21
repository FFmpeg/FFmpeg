FATE_CONCAT_DEMUXER_SIMPLE1_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF)   += mxf
FATE_CONCAT_DEMUXER_SIMPLE1_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF)   += mxf_d10

FATE_CONCAT_DEMUXER_SIMPLE2_LAVF-$(call ENCDEC2, MPEG2VIDEO, MP2, MPEGTS)      += ts

FATE_CONCAT_DEMUXER_EXTENDED_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF)  += mxf
FATE_CONCAT_DEMUXER_EXTENDED_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF)  += mxf_d10

$(foreach D,$(FATE_CONCAT_DEMUXER_SIMPLE1_LAVF-yes),$(eval fate-concat-demuxer-simple1-lavf-$(D): ffprobe$(PROGSSUF)$(EXESUF) fate-lavf-$(D)))
$(foreach D,$(FATE_CONCAT_DEMUXER_SIMPLE1_LAVF-yes),$(eval fate-concat-demuxer-simple1-lavf-$(D): CMD = concat $(SRC_PATH)/tests/simple1.ffconcat ../lavf/lavf.$(D)))
FATE_CONCAT_DEMUXER-$(CONFIG_CONCAT_DEMUXER) += $(FATE_CONCAT_DEMUXER_SIMPLE1_LAVF-yes:%=fate-concat-demuxer-simple1-lavf-%)

$(foreach D,$(FATE_CONCAT_DEMUXER_SIMPLE2_LAVF-yes),$(eval fate-concat-demuxer-simple2-lavf-$(D): ffprobe$(PROGSSUF)$(EXESUF) fate-lavf-$(D)))
$(foreach D,$(FATE_CONCAT_DEMUXER_SIMPLE2_LAVF-yes),$(eval fate-concat-demuxer-simple2-lavf-$(D): CMD = concat $(SRC_PATH)/tests/simple2.ffconcat ../lavf/lavf.$(D)))
FATE_CONCAT_DEMUXER-$(CONFIG_CONCAT_DEMUXER) += $(FATE_CONCAT_DEMUXER_SIMPLE2_LAVF-yes:%=fate-concat-demuxer-simple2-lavf-%)

$(foreach D,$(FATE_CONCAT_DEMUXER_EXTENDED_LAVF-yes),$(eval fate-concat-demuxer-extended-lavf-$(D): ffprobe$(PROGSSUF)$(EXESUF) fate-lavf-$(D)))
$(foreach D,$(FATE_CONCAT_DEMUXER_EXTENDED_LAVF-yes),$(eval fate-concat-demuxer-extended-lavf-$(D): CMD = concat $(SRC_PATH)/tests/extended.ffconcat ../lavf/lavf.$(D) md5))
FATE_CONCAT_DEMUXER-$(CONFIG_CONCAT_DEMUXER) += $(FATE_CONCAT_DEMUXER_EXTENDED_LAVF-yes:%=fate-concat-demuxer-extended-lavf-%)

FATE-$(CONFIG_FFPROBE) += $(FATE_CONCAT_DEMUXER-yes)
