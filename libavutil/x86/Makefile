OBJS += x86/cpu.o                                                       \
        x86/float_dsp_init.o                                            \
        x86/lls_init.o                                                  \

OBJS-$(CONFIG_PIXELUTILS) += x86/pixelutils_init.o                      \

EMMS_OBJS_$(HAVE_INLINE_MMX)_$(HAVE_EXTERNAL_MMX)_$(HAVE_MM_EMPTY) = x86/emms.o

YASM-OBJS += x86/cpuid.o                                                \
             $(EMMS_OBJS_NO_NO_NO)                                      \
             x86/float_dsp.o                                            \
             x86/lls.o                                                  \

YASM-OBJS-$(CONFIG_PIXELUTILS) += x86/pixelutils.o                      \
