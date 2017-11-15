FATE_PIXLET += fate-pixlet-rgb
fate-pixlet-rgb: CMD = framecrc -i $(TARGET_SAMPLES)/pixlet/pixlet_rgb.mov -an -pix_fmt yuv420p16le

FATE_SAMPLES_AVCONV-$(call DEMDEC, MOV, PIXLET) += $(FATE_PIXLET)
fate-pixlet: $(FATE_PIXLET)
