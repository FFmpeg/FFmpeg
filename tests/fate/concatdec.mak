FATE_CONCAT_DEMUXER_SIMPLE1_LAVF  := mxf mxf_d10

FATE_CONCAT_DEMUXER_SIMPLE2_LAVF  := ts

FATE_CONCAT_DEMUXER_EXTENDED_LAVF := mxf mxf_d10

define FATE_CONCAT_DEMUXER_SUITE
$$(addprefix fate-lavf-,$$(FATE_CONCAT_DEMUXER_$(D)_LAVF)): KEEP_FILES ?= 1
FATE_CONCAT_DEMUXER_$(D)_LAVF := $$(filter $$(FATE_LAVF_CONTAINER:fate-lavf-%=%),$$(FATE_CONCAT_DEMUXER_$(D)_LAVF))
endef

$(foreach D,SIMPLE1 SIMPLE2 EXTENDED,$(eval $(FATE_CONCAT_DEMUXER_SUITE)))

$(foreach D,$(FATE_CONCAT_DEMUXER_SIMPLE1_LAVF),$(eval fate-concat-demuxer-simple1-lavf-$(D): fate-lavf-$(D)))
$(foreach D,$(FATE_CONCAT_DEMUXER_SIMPLE1_LAVF),$(eval fate-concat-demuxer-simple1-lavf-$(D): CMD = concat $(SRC_PATH)/tests/simple1.ffconcat ../lavf/lavf.$(D)))
FATE_CONCAT_DEMUXER += $(FATE_CONCAT_DEMUXER_SIMPLE1_LAVF:%=fate-concat-demuxer-simple1-lavf-%)

$(foreach D,$(FATE_CONCAT_DEMUXER_SIMPLE2_LAVF),$(eval fate-concat-demuxer-simple2-lavf-$(D): fate-lavf-$(D)))
$(foreach D,$(FATE_CONCAT_DEMUXER_SIMPLE2_LAVF),$(eval fate-concat-demuxer-simple2-lavf-$(D): CMD = concat $(SRC_PATH)/tests/simple2.ffconcat ../lavf/lavf.$(D)))
FATE_CONCAT_DEMUXER += $(FATE_CONCAT_DEMUXER_SIMPLE2_LAVF:%=fate-concat-demuxer-simple2-lavf-%)

$(foreach D,$(FATE_CONCAT_DEMUXER_EXTENDED_LAVF),$(eval fate-concat-demuxer-extended-lavf-$(D): fate-lavf-$(D)))
$(foreach D,$(FATE_CONCAT_DEMUXER_EXTENDED_LAVF),$(eval fate-concat-demuxer-extended-lavf-$(D): CMD = concat $(SRC_PATH)/tests/extended.ffconcat ../lavf/lavf.$(D) md5))
FATE_CONCAT_DEMUXER += $(FATE_CONCAT_DEMUXER_EXTENDED_LAVF:%=fate-concat-demuxer-extended-lavf-%)

FATE_CONCAT_DEMUXER := $(if $(CONFIG_CONCAT_DEMUXER), $(FATE_CONCAT_DEMUXER))
FATE_FFPROBE += $(FATE_CONCAT_DEMUXER)
