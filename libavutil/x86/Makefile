OBJS += x86/cpu.o                                                       \
        x86/fixed_dsp_init.o                                            \
        x86/float_dsp_init.o                                            \
        x86/lls_init.o                                                  \

OBJS-$(CONFIG_PIXELUTILS) += x86/pixelutils_init.o                      \

EMMS_OBJS_$(HAVE_MMX_INLINE)_$(HAVE_MMX_EXTERNAL)_$(HAVE_MM_EMPTY) = x86/emms.o

YASM-OBJS += x86/cpuid.o                                                \
             $(EMMS_OBJS__yes_)                                      \
             x86/fixed_dsp.o                                            \
             x86/float_dsp.o                                            \
             x86/lls.o                                                  \

YASM-OBJS-$(CONFIG_PIXELUTILS) += x86/pixelutils.o                      \
