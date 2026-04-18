FATE_LIBAVDEVICE-$(CONFIG_JACK_INDEV) += fate-timefilter
fate-timefilter: libavdevice/tests/timefilter$(EXESUF)
fate-timefilter: CMD = run libavdevice/tests/timefilter

tests/data/alsa_nodesc.conf: | tests/data
	$(Q)printf 'pcm.nodesc {\n\ttype null\n\thint {\n\t\tshow on\n\t}\n}\n' > $@

FATE_FFMPEG-$(CONFIG_ALSA_INDEV) += fate-alsa-device-list-nodesc
fate-alsa-device-list-nodesc: tests/data/alsa_nodesc.conf
fate-alsa-device-list-nodesc: CMD = (export ALSA_CONFIG_PATH=$(TARGET_PATH)/tests/data/alsa_nodesc.conf && ffmpeg -sources alsa)
fate-alsa-device-list-nodesc: CMP = null

FATE-$(CONFIG_AVDEVICE) += $(FATE_LIBAVDEVICE-yes)
fate-libavdevice: $(FATE_LIBAVDEVICE-yes)
