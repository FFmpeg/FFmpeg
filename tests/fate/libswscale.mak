FATE_LIBSWSCALE += fate-sws-pixdesc-query
fate-sws-pixdesc-query: libswscale/tests/pixdesc_query$(EXESUF)
fate-sws-pixdesc-query: CMD = run libswscale/tests/pixdesc_query$(EXESUF)

FATE_LIBSWSCALE += $(FATE_LIBSWSCALE-yes)
FATE-$(CONFIG_SWSCALE) += $(FATE_LIBSWSCALE)
fate-libswscale: $(FATE_LIBSWSCALE)
