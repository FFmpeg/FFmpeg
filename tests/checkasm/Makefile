ifeq ($(CONFIG_CHECKASM),yes)

# libavcodec tests
# subsystems
AVCODECOBJS-$(CONFIG_AC3DSP)            += ac3dsp.o
AVCODECOBJS-$(CONFIG_AUDIODSP)          += audiodsp.o
AVCODECOBJS-$(CONFIG_BLOCKDSP)          += blockdsp.o
AVCODECOBJS-$(CONFIG_BSWAPDSP)          += bswapdsp.o
AVCODECOBJS-$(CONFIG_FDCTDSP)           += fdctdsp.o
AVCODECOBJS-$(CONFIG_FMTCONVERT)        += fmtconvert.o
AVCODECOBJS-$(CONFIG_G722DSP)           += g722dsp.o
AVCODECOBJS-$(CONFIG_H263DSP)           += h263dsp.o
AVCODECOBJS-$(CONFIG_H264CHROMA)        += h264chroma.o
AVCODECOBJS-$(CONFIG_H264DSP)           += h264dsp.o
AVCODECOBJS-$(CONFIG_H264PRED)          += h264pred.o
AVCODECOBJS-$(CONFIG_H264QPEL)          += h264qpel.o
AVCODECOBJS-$(CONFIG_HPELDSP)           += hpeldsp.o
AVCODECOBJS-$(CONFIG_HUFFYUVDSP)        += huffyuvdsp.o
AVCODECOBJS-$(CONFIG_HUFFYUVENCDSP)     += huffyuvencdsp.o
AVCODECOBJS-$(CONFIG_IDCTDSP)           += idctdsp.o
AVCODECOBJS-$(CONFIG_LLAUDDSP)          += llauddsp.o
AVCODECOBJS-$(CONFIG_LLVIDDSP)          += llviddsp.o
AVCODECOBJS-$(CONFIG_LLVIDENCDSP)       += llvidencdsp.o
AVCODECOBJS-$(CONFIG_LPC)               += lpc.o
AVCODECOBJS-$(CONFIG_ME_CMP)            += motion.o
AVCODECOBJS-$(CONFIG_MPEGVIDEO)         += mpegvideo_unquantize.o
AVCODECOBJS-$(CONFIG_MPEGVIDEOENCDSP)   += mpegvideoencdsp.o
AVCODECOBJS-$(CONFIG_QPELDSP)           += qpeldsp.o
AVCODECOBJS-$(CONFIG_VC1DSP)            += vc1dsp.o
AVCODECOBJS-$(CONFIG_VP3DSP)            += vp3dsp.o
AVCODECOBJS-$(CONFIG_VP8DSP)            += vp8dsp.o
AVCODECOBJS-$(CONFIG_VIDEODSP)          += videodsp.o

# decoders/encoders
AVCODECOBJS-$(CONFIG_AAC_DECODER)       += aacpsdsp.o \
                                           sbrdsp.o
AVCODECOBJS-$(CONFIG_AAC_ENCODER)       += aacencdsp.o
AVCODECOBJS-$(CONFIG_ALAC_DECODER)      += alacdsp.o
AVCODECOBJS-$(CONFIG_APV_DECODER)       += apv_dsp.o
AVCODECOBJS-$(CONFIG_CAVS_DECODER)      += cavsdsp.o
AVCODECOBJS-$(CONFIG_DCA_DECODER)       += dcadsp.o synth_filter.o
AVCODECOBJS-$(CONFIG_DIRAC_DECODER)     += diracdsp.o
AVCODECOBJS-$(CONFIG_EXR_DECODER)       += exrdsp.o
AVCODECOBJS-$(CONFIG_FLAC_DECODER)      += flacdsp.o
AVCODECOBJS-$(CONFIG_JPEG2000_DECODER)  += jpeg2000dsp.o
AVCODECOBJS-$(CONFIG_MPEG4_DECODER)     += mpeg4videodsp.o
AVCODECOBJS-$(CONFIG_OPUS_DECODER)      += opusdsp.o
AVCODECOBJS-$(CONFIG_PIXBLOCKDSP)       += pixblockdsp.o
AVCODECOBJS-$(CONFIG_HEVC_DECODER)      += hevc_add_res.o hevc_deblock.o hevc_dequant.o \
					   hevc_idct.o hevc_pel.o hevc_pred.o hevc_sao.o
AVCODECOBJS-$(CONFIG_PNG_DECODER)       += png.o
AVCODECOBJS-$(CONFIG_RV34DSP)           += rv34dsp.o
AVCODECOBJS-$(CONFIG_RV40_DECODER)      += rv40dsp.o
AVCODECOBJS-$(CONFIG_SBC_ENCODER)       += sbcdsp.o
AVCODECOBJS-$(CONFIG_SNOW_DECODER)      += snowdsp.o
AVCODECOBJS-$(CONFIG_SVQ1_ENCODER)      += svq1enc.o
AVCODECOBJS-$(CONFIG_TAK_DECODER)       += takdsp.o
AVCODECOBJS-$(CONFIG_UTVIDEO_DECODER)   += utvideodsp.o
AVCODECOBJS-$(CONFIG_V210_DECODER)      += v210dec.o
AVCODECOBJS-$(CONFIG_V210_ENCODER)      += v210enc.o
AVCODECOBJS-$(CONFIG_VORBIS_DECODER)    += vorbisdsp.o
AVCODECOBJS-$(CONFIG_VP6_DECODER)       += vp6dsp.o
AVCODECOBJS-$(CONFIG_VP9_DECODER)       += vp9dsp.o
AVCODECOBJS-$(CONFIG_VVC_DECODER)       += vvc_alf.o vvc_mc.o vvc_sao.o

CHECKASMOBJS-$(CONFIG_AVCODEC)          += $(AVCODECOBJS-yes)

# libavfilter tests
AVFILTEROBJS-$(CONFIG_SCENE_SAD)         += scene_sad.o
AVFILTEROBJS-$(CONFIG_AFIR_FILTER) += af_afir.o
AVFILTEROBJS-$(CONFIG_BLACKDETECT_FILTER) += vf_blackdetect.o
AVFILTEROBJS-$(CONFIG_BLEND_FILTER) += vf_blend.o
AVFILTEROBJS-$(CONFIG_BWDIF_FILTER)      += vf_bwdif.o
AVFILTEROBJS-$(CONFIG_COLORDETECT_FILTER)+= vf_colordetect.o
AVFILTEROBJS-$(CONFIG_COLORSPACE_FILTER) += vf_colorspace.o
AVFILTEROBJS-$(CONFIG_EQ_FILTER)         += vf_eq.o
AVFILTEROBJS-$(CONFIG_FSPP_FILTER)       += vf_fspp.o
AVFILTEROBJS-$(CONFIG_GBLUR_FILTER)      += vf_gblur.o
AVFILTEROBJS-$(CONFIG_HFLIP_FILTER)      += vf_hflip.o
AVFILTEROBJS-$(CONFIG_IDET_FILTER)       += vf_idet.o
AVFILTEROBJS-$(CONFIG_PP7_FILTER)        += vf_pp7.o
AVFILTEROBJS-$(CONFIG_THRESHOLD_FILTER)  += vf_threshold.o
AVFILTEROBJS-$(CONFIG_NLMEANS_FILTER)    += vf_nlmeans.o
AVFILTEROBJS-$(CONFIG_SOBEL_FILTER)      += vf_convolution.o

CHECKASMOBJS-$(CONFIG_AVFILTER) += $(AVFILTEROBJS-yes)

# swscale tests
SWSCALEOBJS                             += sw_gbrp.o            \
                                           sw_ops.o             \
                                           sw_range_convert.o   \
                                           sw_rgb.o             \
                                           sw_scale.o           \
                                           sw_xyz2rgb.o         \
                                           sw_yuv2rgb.o         \
                                           sw_yuv2yuv.o

CHECKASMOBJS-$(CONFIG_SWSCALE)  += $(SWSCALEOBJS)

# libavutil tests
AVUTILOBJS                              += aes.o
AVUTILOBJS                              += av_tx.o
AVUTILOBJS                              += crc.o
AVUTILOBJS                              += fixed_dsp.o
AVUTILOBJS                              += float_dsp.o
AVUTILOBJS                              += lls.o
AVUTILOBJS-$(CONFIG_PIXELUTILS)         += pixelutils.o

CHECKASMOBJS-$(CONFIG_AVUTIL)  += $(AVUTILOBJS) $(AVUTILOBJS-yes)

EXT_CHECKASMOBJS-$(ARCH_AARCH64)            += ext/src/arm/checkasm_64.o
EXT_CHECKASMOBJS-$(ARCH_ARM)                += ext/src/arm/checkasm_32.o
EXT_CHECKASMOBJS-$(ARCH_LOONGARCH)          += ext/src/loongarch/checkasm.o
EXT_CHECKASMOBJS-$(HAVE_RV)                 += ext/src/riscv/callcheck.o
EXT_CHECKASMOBJS-$(HAVE_X86ASM)             += ext/src/x86/checkasm.o

EXT_CHECKASMOBJS += ext/src/arm/cpu.o                  \
                    ext/src/checkasm.o                 \
                    ext/src/cpu.o                      \
                    ext/src/function.o                 \
                    ext/src/perf.o                     \
                    ext/src/perf/arm.o                 \
                    ext/src/perf/linux.o               \
                    ext/src/perf/macos_kperf.o         \
                    ext/src/riscv/cpu.o                \
                    ext/src/signal.o                   \
                    ext/src/stackguard.o               \
                    ext/src/stats.o                    \
                    ext/src/utils.o                    \
                    ext/src/x86/cpu.o                  \

EXT_CHECKASMOBJS += $(EXT_CHECKASMOBJS-yes)
$(EXT_CHECKASMOBJS:%=tests/checkasm/%): CFLAGS      += -I$(SRC_PATH)/tests/checkasm/ext/src
$(EXT_CHECKASMOBJS:%=tests/checkasm/%): ASFLAGS     += -I$(SRC_PATH)/tests/checkasm/ext/src
$(EXT_CHECKASMOBJS:%=tests/checkasm/%): X86ASMFLAGS += -I$(SRC_PATH)/tests/checkasm/ext/src

CHECKASMOBJS += $(CHECKASMOBJS-yes) checkasm.o $(EXT_CHECKASMOBJS)
CHECKASMOBJS := $(sort $(CHECKASMOBJS:%=tests/checkasm/%))

-include $(CHECKASMOBJS:.o=.d)

CHECKASMDIRS := $(sort $(dir $(CHECKASMOBJS)))
$(CHECKASMOBJS): | $(CHECKASMDIRS)
# Prepend the local checkasm include path before any potential external
# (possibly outdated) version of libcheckasm.
$(CHECKASMOBJS): CFLAGS  := -I$(SRC_PATH)/tests/checkasm/ext/include $(CFLAGS)
$(CHECKASMOBJS): ASFLAGS := -I$(SRC_PATH)/tests/checkasm/ext/include $(ASFLAGS)
OUTDIRS += $(CHECKASMDIRS)

# Expose checkasm_config_header_generated.h to checkasm tests as well, as it
# modifies the behavior of include/checkasm/header_config.h
$(CHECKASMOBJS): CFLAGS      += -DCHECKASM_HAVE_HEADER_GENERATED_H
$(CHECKASMOBJS): ASFLAGS     += -DCHECKASM_HAVE_HEADER_GENERATED_H

# Expose checkasm_config_generated.h/asm only to checkasm itself.
$(EXT_CHECKASMOBJS:%=tests/checkasm/%): CFLAGS      += -DCHECKASM_HAVE_GENERATED_H
$(EXT_CHECKASMOBJS:%=tests/checkasm/%): ASFLAGS     += -DCHECKASM_HAVE_GENERATED_H
$(EXT_CHECKASMOBJS:%=tests/checkasm/%): X86ASMFLAGS += -DCHECKASM_HAVE_GENERATED_H

tests/checkasm/checkasm.o: CFLAGS += -Umain

CHECKASM := tests/checkasm/checkasm$(EXESUF)

$(CHECKASM): $(CHECKASMOBJS) $(FF_STATIC_DEP_LIBS)
	$(call LINK,$(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $(CHECKASMOBJS) $(FF_STATIC_DEP_LIBS) $(EXTRALIBS-avcodec) $(EXTRALIBS-avfilter) $(EXTRALIBS-avformat) $(EXTRALIBS-avutil) $(EXTRALIBS-swresample) $(EXTRALIBS) $(EXTRALIBS-checkasm))

run-checkasm: $(CHECKASM)
run-checkasm:
	$(Q)$(TARGET_EXEC) $(TARGET_PATH)/$(CHECKASM)

checkasm: $(CHECKASM)

testclean:: checkasmclean

checkasmclean:
	$(RM) $(CHECKASM) $(CLEANSUFFIXES:%=tests/checkasm/%) $(CLEANSUFFIXES:%=tests/checkasm/$(ARCH)/%) $(CHECKASMOBJS)

.PHONY: checkasm

endif # CONFIG_CHECKASM
