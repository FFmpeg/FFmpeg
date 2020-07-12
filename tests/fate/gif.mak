FATE_GIF += fate-gif-color
fate-gif-color: CMD = framecrc -i $(TARGET_SAMPLES)/gif/tc217.gif -pix_fmt bgra

FATE_GIF += fate-gif-disposal-background
fate-gif-disposal-background: CMD = framecrc -trans_color 0 -i $(TARGET_SAMPLES)/gif/m4nb.gif -pix_fmt bgra

FATE_GIF += fate-gif-disposal-restore
fate-gif-disposal-restore: CMD = framecrc -i $(TARGET_SAMPLES)/gif/banner2.gif -pix_fmt bgra

FATE_GIF += fate-gif-gray
fate-gif-gray: CMD = framecrc -i $(TARGET_SAMPLES)/gif/Newtons_cradle_animation_book_2.gif -pix_fmt bgra

FATE_GIF += fate-gif-deal
fate-gif-deal: CMD = framecrc -i $(TARGET_SAMPLES)/gif/deal.gif -vsync cfr -pix_fmt bgra

fate-gifenc%: fate-gif-color
fate-gifenc%: PIXFMT = $(word 3, $(subst -, ,$(@)))
fate-gifenc%: SRC = $(TARGET_SAMPLES)/gif/tc217.gif
fate-gifenc%: CMD = framecrc -i $(SRC) -c:v gif -pix_fmt $(PIXFMT) -sws_flags +accurate_rnd+bitexact

FATE_GIF_ENC_PIXFMT = rgb8 bgr8 rgb4_byte bgr4_byte gray pal8
FATE_GIF_ENC-$(call ENCDEC, GIF, GIF) = $(FATE_GIF_ENC_PIXFMT:%=fate-gifenc-%)

FATE_GIF += $(FATE_GIF_ENC-yes)
fate-gifenc: $(FATE_GIF_ENC-yes)

FATE_GIF-$(call DEMDEC, GIF, GIF) += $(FATE_GIF)

FATE_SAMPLES_AVCONV += $(FATE_GIF-yes)
fate-gif: $(FATE_GIF-yes)
