clean::
	$(RM) $(CLEANSUFFIXES:%=libavcodec/aarch64/vvc/%)

OBJS-$(CONFIG_VVC_DECODER)				+= aarch64/vvc/dsp_init.o
NEON-OBJS-$(CONFIG_VVC_DECODER)			+= aarch64/vvc/alf.o
