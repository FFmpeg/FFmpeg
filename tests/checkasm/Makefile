# libavcodec tests
# subsystems
AVCODECOBJS-$(CONFIG_AUDIODSP)          += audiodsp.o
AVCODECOBJS-$(CONFIG_BLOCKDSP)          += blockdsp.o
AVCODECOBJS-$(CONFIG_BSWAPDSP)          += bswapdsp.o
AVCODECOBJS-$(CONFIG_FMTCONVERT)        += fmtconvert.o
AVCODECOBJS-$(CONFIG_HUFFYUVDSP)        += huffyuvdsp.o
AVCODECOBJS-$(CONFIG_H264DSP)           += h264dsp.o
AVCODECOBJS-$(CONFIG_H264PRED)          += h264pred.o
AVCODECOBJS-$(CONFIG_H264QPEL)          += h264qpel.o
AVCODECOBJS-$(CONFIG_VP8DSP)            += vp8dsp.o

# decoders/encoders
AVCODECOBJS-$(CONFIG_DCA_DECODER)       += dcadsp.o synth_filter.o
AVCODECOBJS-$(CONFIG_HEVC_DECODER)      += hevc_add_res.o hevc_idct.o hevc_mc.o
AVCODECOBJS-$(CONFIG_V210_ENCODER)      += v210enc.o
AVCODECOBJS-$(CONFIG_VP9_DECODER)       += vp9dsp.o

CHECKASMOBJS-$(CONFIG_AVCODEC)          += $(AVCODECOBJS-yes)


CHECKASMOBJS-$(ARCH_AARCH64)            += aarch64/checkasm.o
CHECKASMOBJS-$(HAVE_ARMV5TE_EXTERNAL)   += arm/checkasm.o
CHECKASMOBJS-$(HAVE_X86ASM)             += x86/checkasm.o

CHECKASMOBJS += $(CHECKASMOBJS-yes) checkasm.o
CHECKASMOBJS := $(sort $(CHECKASMOBJS:%=tests/checkasm/%))

-include $(CHECKASMOBJS:.o=.d)

CHECKASMDIRS := $(sort $(dir $(CHECKASMOBJS)))
$(CHECKASMOBJS): | $(CHECKASMDIRS)
OBJDIRS += $(CHECKASMDIRS)

CHECKASM := tests/checkasm/checkasm$(EXESUF)

$(CHECKASM): $(CHECKASMOBJS) $(FF_STATIC_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $(CHECKASMOBJS) $(FF_STATIC_DEP_LIBS) $(EXTRALIBS-avcodec) $(EXTRALIBS-avutil) $(EXTRALIBS)

checkasm: $(CHECKASM)

clean:: checkasmclean

checkasmclean:
	$(RM) $(CHECKASM) $(CLEANSUFFIXES:%=tests/checkasm/%) $(CLEANSUFFIXES:%=tests/checkasm/$(ARCH)/%)

.PHONY: checkasm
