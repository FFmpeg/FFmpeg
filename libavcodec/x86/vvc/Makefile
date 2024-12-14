clean::
	$(RM) $(CLEANSUFFIXES:%=libavcodec/x86/vvc/%) $(CLEANSUFFIXES:%=libavcodec/x86/h26x/%)

OBJS-$(CONFIG_VVC_DECODER)             += x86/vvc/dsp_init.o        \
                                          x86/h26x/h2656dsp.o
X86ASM-OBJS-$(CONFIG_VVC_DECODER)      += x86/vvc/alf.o             \
                                          x86/vvc/dmvr.o            \
                                          x86/vvc/mc.o              \
                                          x86/vvc/of.o              \
                                          x86/vvc/sad.o             \
                                          x86/vvc/sao.o             \
                                          x86/vvc/sao_10bit.o       \
                                          x86/h26x/h2656_inter.o
