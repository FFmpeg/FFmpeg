clean::
	$(RM) $(CLEANSUFFIXES:%=libavcodec/x86/vvc/%) $(CLEANSUFFIXES:%=libavcodec/x86/h26x/%)

OBJS-$(CONFIG_VVC_DECODER)             += x86/vvc/vvcdsp_init.o \
                                          x86/h26x/h2656dsp.o
X86ASM-OBJS-$(CONFIG_VVC_DECODER)      += x86/vvc/vvc_mc.o       \
                                          x86/h26x/h2656_inter.o
