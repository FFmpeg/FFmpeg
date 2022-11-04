# libavcodec tests
# subsystems
AVCODECOBJS-$(CONFIG_AUDIODSP)          += audiodsp.o
AVCODECOBJS-$(CONFIG_BLOCKDSP)          += blockdsp.o
AVCODECOBJS-$(CONFIG_BSWAPDSP)          += bswapdsp.o
AVCODECOBJS-$(CONFIG_FMTCONVERT)        += fmtconvert.o
AVCODECOBJS-$(CONFIG_G722DSP)           += g722dsp.o
AVCODECOBJS-$(CONFIG_H264DSP)           += h264dsp.o
AVCODECOBJS-$(CONFIG_H264PRED)          += h264pred.o
AVCODECOBJS-$(CONFIG_H264QPEL)          += h264qpel.o
AVCODECOBJS-$(CONFIG_IDCTDSP)           += idctdsp.o
AVCODECOBJS-$(CONFIG_LLVIDDSP)          += llviddsp.o
AVCODECOBJS-$(CONFIG_LLVIDENCDSP)       += llviddspenc.o
AVCODECOBJS-$(CONFIG_LPC)               += lpc.o
AVCODECOBJS-$(CONFIG_ME_CMP)            += motion.o
AVCODECOBJS-$(CONFIG_VC1DSP)            += vc1dsp.o
AVCODECOBJS-$(CONFIG_VP8DSP)            += vp8dsp.o
AVCODECOBJS-$(CONFIG_VIDEODSP)          += videodsp.o

# decoders/encoders
AVCODECOBJS-$(CONFIG_AAC_DECODER)       += aacpsdsp.o \
                                           sbrdsp.o
AVCODECOBJS-$(CONFIG_ALAC_DECODER)      += alacdsp.o
AVCODECOBJS-$(CONFIG_DCA_DECODER)       += synth_filter.o
AVCODECOBJS-$(CONFIG_EXR_DECODER)       += exrdsp.o
AVCODECOBJS-$(CONFIG_FLAC_DECODER)      += flacdsp.o
AVCODECOBJS-$(CONFIG_HUFFYUV_DECODER)   += huffyuvdsp.o
AVCODECOBJS-$(CONFIG_JPEG2000_DECODER)  += jpeg2000dsp.o
AVCODECOBJS-$(CONFIG_OPUS_DECODER)      += opusdsp.o
AVCODECOBJS-$(CONFIG_PIXBLOCKDSP)       += pixblockdsp.o
AVCODECOBJS-$(CONFIG_HEVC_DECODER)      += hevc_add_res.o hevc_idct.o hevc_sao.o hevc_pel.o
AVCODECOBJS-$(CONFIG_UTVIDEO_DECODER)   += utvideodsp.o
AVCODECOBJS-$(CONFIG_V210_DECODER)      += v210dec.o
AVCODECOBJS-$(CONFIG_V210_ENCODER)      += v210enc.o
AVCODECOBJS-$(CONFIG_VORBIS_DECODER)    += vorbisdsp.o
AVCODECOBJS-$(CONFIG_VP9_DECODER)       += vp9dsp.o

CHECKASMOBJS-$(CONFIG_AVCODEC)          += $(AVCODECOBJS-yes)

# libavfilter tests
AVFILTEROBJS-$(CONFIG_AFIR_FILTER) += af_afir.o
AVFILTEROBJS-$(CONFIG_BLEND_FILTER) += vf_blend.o
AVFILTEROBJS-$(CONFIG_COLORSPACE_FILTER) += vf_colorspace.o
AVFILTEROBJS-$(CONFIG_EQ_FILTER)         += vf_eq.o
AVFILTEROBJS-$(CONFIG_GBLUR_FILTER)      += vf_gblur.o
AVFILTEROBJS-$(CONFIG_HFLIP_FILTER)      += vf_hflip.o
AVFILTEROBJS-$(CONFIG_THRESHOLD_FILTER)  += vf_threshold.o
AVFILTEROBJS-$(CONFIG_NLMEANS_FILTER)    += vf_nlmeans.o
AVFILTEROBJS-$(CONFIG_SOBEL_FILTER)      += vf_convolution.o

CHECKASMOBJS-$(CONFIG_AVFILTER) += $(AVFILTEROBJS-yes)

# swscale tests
SWSCALEOBJS                             += sw_gbrp.o sw_rgb.o sw_scale.o

CHECKASMOBJS-$(CONFIG_SWSCALE)  += $(SWSCALEOBJS)

# libavutil tests
AVUTILOBJS                              += av_tx.o
AVUTILOBJS                              += fixed_dsp.o
AVUTILOBJS                              += float_dsp.o

CHECKASMOBJS-$(CONFIG_AVUTIL)  += $(AVUTILOBJS)

CHECKASMOBJS-$(ARCH_AARCH64)            += aarch64/checkasm.o
CHECKASMOBJS-$(HAVE_ARMV5TE_EXTERNAL)   += arm/checkasm.o
CHECKASMOBJS-$(ARCH_RISCV)              += riscv/checkasm.o
CHECKASMOBJS-$(HAVE_X86ASM)             += x86/checkasm.o

CHECKASMOBJS += $(CHECKASMOBJS-yes) checkasm.o
CHECKASMOBJS := $(sort $(CHECKASMOBJS:%=tests/checkasm/%))

-include $(CHECKASMOBJS:.o=.d)

CHECKASMDIRS := $(sort $(dir $(CHECKASMOBJS)))
$(CHECKASMOBJS): | $(CHECKASMDIRS)
OUTDIRS += $(CHECKASMDIRS)

tests/checkasm/checkasm.o: CFLAGS += -Umain

CHECKASM := tests/checkasm/checkasm$(EXESUF)

$(CHECKASM): $(CHECKASMOBJS) $(FF_STATIC_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $(CHECKASMOBJS) $(FF_STATIC_DEP_LIBS) $(EXTRALIBS-avcodec) $(EXTRALIBS-avfilter) $(EXTRALIBS-avformat) $(EXTRALIBS-avutil) $(EXTRALIBS-swresample) $(EXTRALIBS)

checkasm: $(CHECKASM)

testclean:: checkasmclean

checkasmclean:
	$(RM) $(CHECKASM) $(CLEANSUFFIXES:%=tests/checkasm/%) $(CLEANSUFFIXES:%=tests/checkasm/$(ARCH)/%)

.PHONY: checkasm
