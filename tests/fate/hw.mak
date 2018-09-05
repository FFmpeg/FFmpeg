FATE_HWCONTEXT += fate-hwdevice
fate-hwdevice: libavutil/tests/hwdevice$(EXESUF)
fate-hwdevice: CMD = run libavutil/tests/hwdevice
fate-hwdevice: CMP = null

FATE_HW-$(CONFIG_AVUTIL) += $(FATE_HWCONTEXT)
