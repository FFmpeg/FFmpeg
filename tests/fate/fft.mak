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

$(FATE_FFT_ALL): libavcodec/fft-test$(EXESUF)
$(FATE_FFT_ALL): CMD = run libavcodec/fft-test $(CPUFLAGS:%=-c%) $(ARGS)

define DEF_FFT_FIXED
FATE_FFT_FIXED-$(CONFIG_FFT)   += fate-fft-fixed-$(1)  fate-ifft-fixed-$(1)
FATE_MDCT_FIXED-$(CONFIG_MDCT) += fate-mdct-fixed-$(1) fate-imdct-fixed-$(1)

fate-fft-fixed-$(1):   ARGS = -n$(1)
fate-ifft-fixed-$(1):  ARGS = -n$(1) -i
fate-mdct-fixed-$(1):  ARGS = -n$(1) -m
fate-imdct-fixed-$(1): ARGS = -n$(1) -m -i
endef

$(foreach N, 4 5 6 7 8 9 10 11 12, $(eval $(call DEF_FFT_FIXED,$(N))))

fate-fft-fixed: $(FATE_FFT_FIXED-yes)
fate-mdct-fixed: $(FATE_MDCT_FIXED-yes)

FATE_FFT_FIXED_ALL = $(FATE_FFT_FIXED-yes) $(FATE_MDCT_FIXED-yes)

$(FATE_FFT_FIXED_ALL): libavcodec/fft-fixed-test$(EXESUF)
$(FATE_FFT_FIXED_ALL): CMD = run libavcodec/fft-fixed-test $(CPUFLAGS:%=-c%) $(ARGS)

$(FATE_FFT_ALL) $(FATE_FFT_FIXED_ALL): REF = /dev/null

define DEF_FFT_FIXED32
FATE_FFT_FIXED32 += fate-fft-fixed32-$(1)   fate-ifft-fixed32-$(1)  \
                  fate-mdct-fixed32-$(1) fate-imdct-fixed32-$(1)

fate-fft-fixed32-$(1):   ARGS = -n$(1)
fate-ifft-fixed32-$(1):  ARGS = -n$(1) -i
#fate-mdct-fixed32-$(1):  ARGS = -n$(1) -m
fate-imdct-fixed32-$(1): ARGS = -n$(1) -m -i
endef

$(foreach N, 4 5 6 7 8 9 10 11 12, $(eval $(call DEF_FFT_FIXED32,$(N))))

fate-fft-fixed32-test: $(FATE_FFT_FIXED32)
$(FATE_FFT_FIXED32): libavcodec/fft-fixed32-test$(EXESUF)
$(FATE_FFT_FIXED32): CMD = run libavcodec/fft-fixed32-test $(CPUFLAGS:%=-c%) $(ARGS)
$(FATE_FFT_FIXED32): REF = /dev/null

fate-dct: fate-dct-float
fate-fft: fate-fft-float fate-fft-fixed fate-fft-fixed32-test
fate-mdct: fate-mdct-float fate-mdct-fixed
fate-rdft: fate-rdft-float

FATE-$(call ALLYES, AVCODEC FFT MDCT) += $(FATE_FFT_ALL) $(FATE_FFT_FIXED_ALL) $(FATE_FFT_FIXED32)
fate-fft-all: $(FATE_FFT_ALL) $(FATE_FFT_FIXED_ALL) $(FATE_FFT_FIXED32)
