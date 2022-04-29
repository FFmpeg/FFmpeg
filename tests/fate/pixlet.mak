FATE_PIXLET-$(call FRAMECRC, MOV, PIXLET, SCALE_FILTER) += fate-pixlet-rgb
fate-pixlet-rgb: CMD = framecrc -i $(TARGET_SAMPLES)/pixlet/pixlet_rgb.mov -an -pix_fmt yuv420p16le -vf scale

FATE_SAMPLES_FFMPEG += $(FATE_PIXLET-yes)
fate-pixlet: $(FATE_PIXLET-yes)
