FATE_HWCONTEXT += fate-hwdevice
fate-hwdevice: libavutil/tests/hwdevice$(EXESUF)
fate-hwdevice: CMD = run libavutil/tests/hwdevice$(EXESUF)
fate-hwdevice: CMP = null

FATE_HW-$(CONFIG_CUDA) += fate-hwcontext-cuda
fate-hwcontext-cuda: libavutil/tests/hwcontext_cuda$(EXESUF)
fate-hwcontext-cuda: CMD = run libavutil/tests/hwcontext_cuda$(EXESUF)
fate-hwcontext-cuda: CMP = grep
fate-hwcontext-cuda: REF = ^\(source reuse checksum: 178f1010\|No CUDA device available, skipping\.\)$$

FATE_HW-$(CONFIG_AVUTIL) += $(FATE_HWCONTEXT)
