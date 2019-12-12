FATE_LAVF_IMAGE2PIPE-$(call ENCDEC,     PBM,    IMAGE2PIPE)     += pbmpipe
FATE_LAVF_IMAGE2PIPE-$(call ENCDEC,     PGM,    IMAGE2PIPE)     += pgmpipe
FATE_LAVF_IMAGE2PIPE-$(call ENCDEC,     PPM,    IMAGE2PIPE)     += ppmpipe

FATE_LAVF_IMAGE2PIPE = $(FATE_LAVF_IMAGE2PIPE-yes:%=fate-lavf-%)

$(FATE_LAVF_IMAGE2PIPE): CMD = lavf_image2pipe
$(FATE_LAVF_IMAGE2PIPE): REF = $(SRC_PATH)/tests/ref/lavf/$(@:fate-lavf-%=%)
$(FATE_LAVF_IMAGE2PIPE): $(VREF)

FATE_AVCONV += $(FATE_LAVF_IMAGE2PIPE)
fate-lavf-image2pipe fate-lavf: $(FATE_LAVF_IMAGE2PIPE)
