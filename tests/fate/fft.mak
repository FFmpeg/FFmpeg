FATE_FFT += fate-fft
fate-fft: libavcodec/fft-test$(EXESUF)
fate-fft: CMD = run libavcodec/fft-test

FATE_FFT += fate-ifft
fate-ifft: libavcodec/fft-test$(EXESUF)
fate-ifft: CMD = run libavcodec/fft-test -i

FATE_FFT += fate-mdct
fate-mdct: libavcodec/fft-test$(EXESUF)
fate-mdct: CMD = run libavcodec/fft-test -m

FATE_FFT += fate-imdct
fate-imdct: libavcodec/fft-test$(EXESUF)
fate-imdct: CMD = run libavcodec/fft-test -m -i

FATE_FFT += fate-rdft
fate-rdft: libavcodec/fft-test$(EXESUF)
fate-rdft: CMD = run libavcodec/fft-test -r

FATE_FFT += fate-irdft
fate-irdft: libavcodec/fft-test$(EXESUF)
fate-irdft: CMD = run libavcodec/fft-test -r -i

FATE_FFT += fate-dct1d
fate-dct1d: libavcodec/fft-test$(EXESUF)
fate-dct1d: CMD = run libavcodec/fft-test -d

FATE_FFT += fate-idct1d
fate-idct1d: libavcodec/fft-test$(EXESUF)
fate-idct1d: CMD = run libavcodec/fft-test -d -i

FATE_TESTS += $(FATE_FFT)
fate-fft-test: $(FATE_FFT)
$(FATE_FFT): REF = /dev/null
