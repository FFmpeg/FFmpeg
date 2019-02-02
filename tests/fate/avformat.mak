FATE_LAVF-$(call ENCDEC,  GIF,                   IMAGE2)             += gif
FATE_LAVF-$(CONFIG_YUV4MPEGPIPE_MUXER)                               += yuv4mpeg

FATE_LAVF += $(FATE_LAVF-yes:%=fate-lavf-%)

$(FATE_LAVF): $(AREF) $(VREF)
$(FATE_LAVF): CMD = lavftest

FATE_AVCONV += $(FATE_LAVF)
fate-lavf:     $(FATE_LAVF)
