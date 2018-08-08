FATE_LAVF_VIDEO-$(call ENCDEC,  GIF,        IMAGE2)             += gif
FATE_LAVF_VIDEO-$(CONFIG_YUV4MPEGPIPE_MUXER)                    += y4m

FATE_LAVF_VIDEO = $(FATE_LAVF_VIDEO-yes:%=fate-lavf-%)

$(FATE_LAVF_VIDEO): CMD = lavf_video
$(FATE_LAVF_VIDEO): REF = $(SRC_PATH)/tests/ref/lavf/$(@:fate-lavf-%=%)
$(FATE_LAVF_VIDEO): $(VREF)

fate-lavf-gif: CMD = lavf_video "-pix_fmt rgb24"

FATE_AVCONV += $(FATE_LAVF_VIDEO)
fate-lavf-video fate-lavf: $(FATE_LAVF_VIDEO)
