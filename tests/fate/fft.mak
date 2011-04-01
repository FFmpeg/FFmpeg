FATE_FFT = fate-fft   fate-ifft   \
           fate-mdct  fate-imdct  \
           fate-rdft  fate-irdft  \
           fate-dct1d fate-idct1d

fate-fft:    CMD = run libavcodec/fft-test
fate-ifft:   CMD = run libavcodec/fft-test -i
fate-mdct:   CMD = run libavcodec/fft-test -m
fate-imdct:  CMD = run libavcodec/fft-test -m -i
fate-rdft:   CMD = run libavcodec/fft-test -r
fate-irdft:  CMD = run libavcodec/fft-test -r -i
fate-dct1d:  CMD = run libavcodec/fft-test -d
fate-idct1d: CMD = run libavcodec/fft-test -d -i

fate-fft-test: $(FATE_FFT)
$(FATE_FFT): libavcodec/fft-test$(EXESUF)
$(FATE_FFT): REF = /dev/null

FATE_FFT_FIXED = fate-fft-fixed  fate-ifft-fixed  \
                 fate-mdct-fixed fate-imdct-fixed

fate-fft-fixed:   CMD = run libavcodec/fft-fixed-test
fate-ifft-fixed:  CMD = run libavcodec/fft-fixed-test -i
fate-mdct-fixed:  CMD = run libavcodec/fft-fixed-test -m
fate-imdct-fixed: CMD = run libavcodec/fft-fixed-test -m -i

fate-fft-fixed-test: $(FATE_FFT_FIXED)
$(FATE_FFT_FIXED): libavcodec/fft-fixed-test$(EXESUF)
$(FATE_FFT_FIXED): REF = /dev/null

FATE_TESTS += $(FATE_FFT) $(FATE_FFT_FIXED)
