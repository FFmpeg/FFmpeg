FATE_CDXL += fate-cdxl-bitline-ham6
fate-cdxl-bitline-ham6: CMD = framecrc -i $(TARGET_SAMPLES)/cdxl/bitline.cdxl -frames:v 10

FATE_CDXL += fate-cdxl-ham6
fate-cdxl-ham6: CMD = framecrc -i $(TARGET_SAMPLES)/cdxl/cat.cdxl -an -frames:v 16

FATE_CDXL += fate-cdxl-ham8
fate-cdxl-ham8: CMD = framecrc -i $(TARGET_SAMPLES)/cdxl/mirage.cdxl -an -frames:v 1

FATE_CDXL += fate-cdxl-pal8
fate-cdxl-pal8: CMD = framecrc -i $(TARGET_SAMPLES)/cdxl/maku.cdxl -pix_fmt rgb24 -frames:v 11 -vf scale

FATE_CDXL += fate-cdxl-pal8-small
fate-cdxl-pal8-small: CMD = framecrc -i $(TARGET_SAMPLES)/cdxl/fruit.cdxl -an -pix_fmt rgb24 -frames:v 46 -vf scale

FATE_CDXL-$(call DEMDEC, CDXL, CDXL) += $(FATE_CDXL)

FATE_SAMPLES_AVCONV += $(FATE_CDXL-yes)
fate-cdxl: $(FATE_CDXL-yes)
