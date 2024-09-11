clean::
	$(RM) $(CLEANSUFFIXES:%=libavcodec/aarch64/vvc/%)

OBJS-$(CONFIG_VVC_DECODER)              += aarch64/vvc/dsp_init.o
NEON-OBJS-$(CONFIG_VVC_DECODER)         += aarch64/vvc/alf.o \
                                           aarch64/vvc/inter.o \
                                           aarch64/vvc/sad.o \
                                           aarch64/h26x/epel_neon.o \
                                           aarch64/h26x/qpel_neon.o \
                                           aarch64/h26x/sao_neon.o
