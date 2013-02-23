FATE_GIF += fate-gif-color
fate-gif-color: CMD = framecrc -i $(SAMPLES)/gif/tc217.gif -pix_fmt bgra

FATE_GIF += fate-gif-disposal-restore
fate-gif-disposal-restore: CMD = framecrc -i $(SAMPLES)/gif/banner2.gif -pix_fmt bgra

FATE_GIF += fate-gif-gray
fate-gif-gray: CMD = framecrc -i $(SAMPLES)/gif/Newtons_cradle_animation_book_2.gif -pix_fmt bgra

FATE_GIF-$(call DEMDEC, GIF, GIF) += $(FATE_GIF)

FATE_SAMPLES_AVCONV += $(FATE_GIF-yes)
fate-gif: $(FATE_GIF-yes)
