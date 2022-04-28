FATE_GIF += fate-gif-color
fate-gif-color: CMD = framecrc -i $(TARGET_SAMPLES)/gif/tc217.gif -pix_fmt bgra -vf scale

FATE_GIF += fate-gif-disposal-background
fate-gif-disposal-background: CMD = framecrc -trans_color 0 -i $(TARGET_SAMPLES)/gif/m4nb.gif -pix_fmt bgra -vf scale

FATE_GIF += fate-gif-disposal-restore
fate-gif-disposal-restore: CMD = framecrc -i $(TARGET_SAMPLES)/gif/banner2.gif -pix_fmt bgra -vf scale

FATE_GIF += fate-gif-gray
fate-gif-gray: CMD = framecrc -i $(TARGET_SAMPLES)/gif/Newtons_cradle_animation_book_2.gif -pix_fmt bgra -vf scale

FATE_GIF += fate-gif-deal
fate-gif-deal: CMD = framecrc -i $(TARGET_SAMPLES)/gif/deal.gif -vsync cfr -pix_fmt bgra -auto_conversion_filters

FATE_GIF-$(call FRAMECRC, GIF, GIF, SCALE_FILTER) += $(FATE_GIF)

fate-gifenc%: PIXFMT = $(word 3, $(subst -, ,$(@)))
fate-gifenc%: SRC = $(TARGET_SAMPLES)/gif/tc217.gif
fate-gifenc%: CMD = framecrc -i $(SRC) -c:v gif -pix_fmt $(PIXFMT) -sws_flags +accurate_rnd+bitexact -vf scale

FATE_GIF_ENC_PIXFMT = rgb8 bgr8 rgb4_byte bgr4_byte gray pal8
FATE_GIF_ENC-$(call ENCDEC, GIF, FRAMECRC GIF, SCALE_FILTER PIPE_PROTOCOL) = $(FATE_GIF_ENC_PIXFMT:%=fate-gifenc-%)

fate-gifenc: $(FATE_GIF_ENC-yes)

FATE_SAMPLES_FFMPEG += $(FATE_GIF-yes) $(FATE_GIF_ENC-yes)
fate-gif: $(FATE_GIF-yes) $(FATE_GIF_ENC-yes)
