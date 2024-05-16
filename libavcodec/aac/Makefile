clean::
		$(RM) $(CLEANSUFFIXES:%=libavcodec/aac/%)

OBJS-$(CONFIG_AAC_DECODER)          +=  aac/aacdec.o aac/aacdec_tab.o \
                                        aac/aacdec_float.o aac/aacdec_usac.o \
                                        aac/aacdec_ac.o aac/aacdec_lpd.o
OBJS-$(CONFIG_AAC_FIXED_DECODER)    +=  aac/aacdec.o aac/aacdec_tab.o \
                                        aac/aacdec_fixed.o
