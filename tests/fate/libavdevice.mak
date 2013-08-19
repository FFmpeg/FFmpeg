FATE_LIBAVDEVICE-yes += fate-timefilter
fate-timefilter: libavdevice/timefilter-test$(EXESUF)
fate-timefilter: CMD = run libavdevice/timefilter-test

FATE-$(CONFIG_AVDEVICE) += $(FATE_LIBAVDEVICE-yes)
fate-libavdevice: $(FATE_LIBAVDEVICE-yes)
