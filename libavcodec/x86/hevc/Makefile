clean::
	$(RM) $(CLEANSUFFIXES:%=libavcodec/x86/hevc/%) $(CLEANSUFFIXES:%=libavcodec/x86/h26x/%)

OBJS-$(CONFIG_HEVC_DECODER)             += x86/hevc/dsp_init.o      \
                                           x86/h26x/h2656dsp.o
X86ASM-OBJS-$(CONFIG_HEVC_DECODER)      += x86/hevc/add_res.o       \
                                           x86/hevc/deblock.o       \
                                           x86/hevc/idct.o          \
                                           x86/hevc/mc.o            \
                                           x86/hevc/sao.o           \
                                           x86/hevc/sao_10bit.o     \
                                           x86/h26x/h2656_inter.o
