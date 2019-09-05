FATE_LIBAVDEVICE-$(CONFIG_JACK_INDEV) += fate-timefilter
fate-timefilter: libavdevice/tests/timefilter$(EXESUF)
fate-timefilter: CMD = run libavdevice/tests/timefilter

FATE-$(CONFIG_AVDEVICE) += $(FATE_LIBAVDEVICE-yes)
fate-libavdevice: $(FATE_LIBAVDEVICE-yes)
