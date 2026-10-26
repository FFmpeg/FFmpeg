OBJS += ppc/cpu.o                                                       \
        ppc/float_dsp_init.o                                            \

ALTIVEC-OBJS += ppc/float_dsp_altivec.o                                 \

VSX-OBJS     += ppc/float_dsp_vsx.o                                     \

$(SUBDIR)ppc/util_altivec.h.o: CFLAGS += $(ALTIVECFLAGS)
