define DEF_FFT
FATE_FFT += fate-fft-$(1)   fate-ifft-$(1)   \
            fate-mdct-$(1)  fate-imdct-$(1)  \
            fate-rdft-$(1)  fate-irdft-$(1)  \
            fate-dct1d-$(1) fate-idct1d-$(1)

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

fate-fft-test: $(FATE_FFT)
$(FATE_FFT): libavcodec/fft-test$(EXESUF)
$(FATE_FFT): CMD = run libavcodec/fft-test $(CPUFLAGS:%=-c%) $(ARGS)
$(FATE_FFT): REF = /dev/null

define DEF_FFT_FIXED
FATE_FFT_FIXED += fate-fft-fixed-$(1)   fate-ifft-fixed-$(1)  \
                  fate-mdct-fixed-$(1) fate-imdct-fixed-$(1)

fate-fft-fixed-$(1):   ARGS = -n$(1)
fate-ifft-fixed-$(1):  ARGS = -n$(1) -i
fate-mdct-fixed-$(1):  ARGS = -n$(1) -m
fate-imdct-fixed-$(1): ARGS = -n$(1) -m -i
endef

$(foreach N, 4 5 6 7 8 9 10 11 12, $(eval $(call DEF_FFT_FIXED,$(N))))

fate-fft-fixed-test: $(FATE_FFT_FIXED)
$(FATE_FFT_FIXED): libavcodec/fft-fixed-test$(EXESUF)
$(FATE_FFT_FIXED): CMD = run libavcodec/fft-fixed-test $(CPUFLAGS:%=-c%) $(ARGS)
$(FATE_FFT_FIXED): REF = /dev/null

FATE-$(CONFIG_FFT) += $(FATE_FFT) $(FATE_FFT_FIXED)
fate-fft: $(FATE_FFT) $(FATE_FFT_FIXED)
