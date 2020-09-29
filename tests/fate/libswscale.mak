FATE_LIBSWSCALE += fate-sws-pixdesc-query
fate-sws-pixdesc-query: libswscale/tests/pixdesc_query$(EXESUF)
fate-sws-pixdesc-query: CMD = run libswscale/tests/pixdesc_query$(EXESUF)

FATE_LIBSWSCALE += fate-sws-floatimg-cmp
fate-sws-floatimg-cmp: libswscale/tests/floatimg_cmp$(EXESUF)
fate-sws-floatimg-cmp: CMD = run libswscale/tests/floatimg_cmp$(EXESUF)

FATE_LIBSWSCALE += $(FATE_LIBSWSCALE-yes)
FATE-$(CONFIG_SWSCALE) += $(FATE_LIBSWSCALE)
fate-libswscale: $(FATE_LIBSWSCALE)
