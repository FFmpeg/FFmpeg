# libavcodec tests
AVCODECOBJS-$(CONFIG_ALAC_DECODER) += alacdsp.o
AVCODECOBJS-$(CONFIG_BSWAPDSP) += bswapdsp.o
AVCODECOBJS-$(CONFIG_DCA_DECODER) += synth_filter.o
AVCODECOBJS-$(CONFIG_FLACDSP)  += flacdsp.o
AVCODECOBJS-$(CONFIG_FMTCONVERT)   += fmtconvert.o
AVCODECOBJS-$(CONFIG_H264PRED) += h264pred.o
AVCODECOBJS-$(CONFIG_H264QPEL) += h264qpel.o
AVCODECOBJS-$(CONFIG_JPEG2000_DECODER) += jpeg2000dsp.o
AVCODECOBJS-$(CONFIG_PIXBLOCKDSP) += pixblockdsp.o
AVCODECOBJS-$(CONFIG_V210_ENCODER) += v210enc.o
AVCODECOBJS-$(CONFIG_VP9_DECODER) += vp9dsp.o
AVCODECOBJS-$(CONFIG_VIDEODSP) += videodsp.o

CHECKASMOBJS-$(CONFIG_AVCODEC) += $(AVCODECOBJS-yes)


-include $(SRC_PATH)/tests/checkasm/$(ARCH)/Makefile

CHECKASMOBJS += $(CHECKASMOBJS-yes) checkasm.o
CHECKASMOBJS := $(sort $(CHECKASMOBJS:%=tests/checkasm/%))

-include $(CHECKASMOBJS:.o=.d)

CHECKASMDIRS := $(sort $(dir $(CHECKASMOBJS)))
$(CHECKASMOBJS): | $(CHECKASMDIRS)
OBJDIRS += $(CHECKASMDIRS)

tests/checkasm/checkasm.o: CFLAGS += -Umain

CHECKASM := tests/checkasm/checkasm$(EXESUF)

$(CHECKASM): $(EXEOBJS) $(CHECKASMOBJS) $(FF_STATIC_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $(CHECKASMOBJS) $(FF_STATIC_DEP_LIBS) $(EXTRALIBS)

checkasm: $(CHECKASM)

testclean:: checkasmclean

checkasmclean:
	$(RM) $(CHECKASM) $(CLEANSUFFIXES:%=tests/checkasm/%) $(CLEANSUFFIXES:%=tests/checkasm/$(ARCH)/%)

.PHONY: checkasm
