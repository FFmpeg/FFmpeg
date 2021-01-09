define DEF_FFT
FATE_DCT-$(CONFIG_DCT)   += fate-dct1d-$(1) fate-idct1d-$(1)
FATE_FFT-$(CONFIG_FFT)   += fate-fft-$(1)   fate-ifft-$(1)
FATE_MDCT-$(CONFIG_MDCT) += fate-mdct-$(1)  fate-imdct-$(1)
FATE_RDFT-$(CONFIG_RDFT) += fate-rdft-$(1)  fate-irdft-$(1)

fate-fft-$(N):    ARGS = -n$(1)
fate-ifft-$(N):   ARGS = -n$(1) -i
fate-mdct-$(N):   ARGS = -n$(1) -m
fate-imdct-$(N):  ARGS = -n$(1) -m -i
fate-rdft-$(N):   ARGS = -n$(1) -r
fate-irdft-$(N):  ARGS = -n$(1) -r -i
fate-dct1d-$(N):  ARGS = -n$(1) -d
fate-idct1d-$(N): ARGS = -n$(1) -d -i
endef

$(foreach N, 4 5 6 7 8 9 10 11 12, $(eval $(call DEF_FFT,$(N))))

fate-dct-float: $(FATE_DCT-yes)
fate-fft-float: $(FATE_FFT-yes)
fate-mdct-float: $(FATE_MDCT-yes)
fate-rdft-float: $(FATE_RDFT-yes)

FATE_FFT_ALL = $(FATE_DCT-yes) $(FATE_FFT-yes) $(FATE_MDCT-yes) $(FATE_RDFT-yes)

$(FATE_FFT_ALL): libavcodec/tests/fft$(EXESUF)
$(FATE_FFT_ALL): CMD = run libavcodec/tests/fft$(EXESUF) $(CPUFLAGS:%=-c%) $(ARGS)

$(FATE_FFT_ALL): CMP = null

define DEF_FFT_FIXED32
FATE_FFT_FIXED32 += fate-fft-fixed32-$(1)   fate-ifft-fixed32-$(1)  \
                  fate-mdct-fixed32-$(1) fate-imdct-fixed32-$(1)

fate-fft-fixed32-$(1):   ARGS = -n$(1)
fate-ifft-fixed32-$(1):  ARGS = -n$(1) -i
#fate-mdct-fixed32-$(1):  ARGS = -n$(1) -m
fate-imdct-fixed32-$(1): ARGS = -n$(1) -m -i
endef

$(foreach N, 4 5 6 7 8 9 10 11 12, $(eval $(call DEF_FFT_FIXED32,$(N))))

fate-fft-fixed32: $(FATE_FFT_FIXED32)
$(FATE_FFT_FIXED32): libavcodec/tests/fft-fixed32$(EXESUF)
$(FATE_FFT_FIXED32): CMD = run libavcodec/tests/fft-fixed32$(EXESUF) $(CPUFLAGS:%=-c%) $(ARGS)
$(FATE_FFT_FIXED32): CMP = null

define DEF_AV_FFT
FATE_AV_DCT-$(CONFIG_DCT)   += fate-av-dct1d-$(1) fate-av-idct1d-$(1)
FATE_AV_FFT-$(CONFIG_FFT)   += fate-av-fft-$(1)   fate-av-ifft-$(1)
FATE_AV_MDCT-$(CONFIG_MDCT) += fate-av-mdct-$(1)  fate-av-imdct-$(1)
FATE_AV_RDFT-$(CONFIG_RDFT) += fate-av-rdft-$(1)  fate-av-irdft-$(1)

fate-av-fft-$(N):    ARGS = -n$(1)
fate-av-ifft-$(N):   ARGS = -n$(1) -i
fate-av-mdct-$(N):   ARGS = -n$(1) -m
fate-av-imdct-$(N):  ARGS = -n$(1) -m -i
fate-av-rdft-$(N):   ARGS = -n$(1) -r
fate-av-irdft-$(N):  ARGS = -n$(1) -r -i
fate-av-dct1d-$(N):  ARGS = -n$(1) -d
fate-av-idct1d-$(N): ARGS = -n$(1) -d -i
endef

$(foreach N, 4 5 6 7 8 9 10 11 12, $(eval $(call DEF_AV_FFT,$(N))))

fate-av-dct-float: $(FATE_AV_DCT-yes)
fate-av-fft-float: $(FATE_AV_FFT-yes)
fate-av-mdct-float: $(FATE_AV_MDCT-yes)
fate-av-rdft-float: $(FATE_AV_RDFT-yes)

FATE_AV_FFT_ALL = $(FATE_AV_DCT-yes) $(FATE_AV_FFT-yes) $(FATE_AV_MDCT-yes) $(FATE_AV_RDFT-yes)

$(FATE_AV_FFT_ALL): libavcodec/tests/avfft$(EXESUF)
$(FATE_AV_FFT_ALL): CMD = run libavcodec/tests/avfft$(EXESUF) $(CPUFLAGS:%=-c%) $(ARGS)
$(FATE_AV_FFT_ALL): CMP = null

fate-dct: fate-dct-float
fate-fft: fate-fft-float fate-fft-fixed32
fate-mdct: fate-mdct-float
fate-rdft: fate-rdft-float

FATE-$(call ALLYES, AVCODEC FFT MDCT) += $(FATE_FFT_ALL) $(FATE_FFT_FIXED32) $(FATE_AV_FFT_ALL)
fate-fft-all: $(FATE_FFT_ALL) $(FATE_FFT_FIXED32) $(FATE_AV_FFT_ALL)
