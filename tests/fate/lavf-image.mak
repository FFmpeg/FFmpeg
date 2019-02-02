FATE_LAVF_IMAGES-$(call ENCDEC,  BMP,            IMAGE2)             += bmp
FATE_LAVF_IMAGES-$(call ENCDEC,  DPX,            IMAGE2)             += dpx
FATE_LAVF_IMAGES-$(call ENCDEC,  MJPEG,          IMAGE2)             += jpg
FATE_LAVF_IMAGES-$(call ENCDEC,  PAM,            IMAGE2)             += pam
FATE_LAVF_IMAGES-$(call ENCDEC,  PCX,            IMAGE2)             += pcx
FATE_LAVF_IMAGES-$(call ENCDEC,  PGM,            IMAGE2)             += pgm
FATE_LAVF_IMAGES-$(call ENCDEC,  PNG,            IMAGE2)             += png
FATE_LAVF_IMAGES-$(call ENCDEC,  PPM,            IMAGE2)             += ppm
FATE_LAVF_IMAGES-$(call ENCDEC,  SGI,            IMAGE2)             += sgi
FATE_LAVF_IMAGES-$(call ENCDEC,  SUNRAST,        IMAGE2)             += sun
FATE_LAVF_IMAGES-$(call ENCDEC,  TARGA,          IMAGE2)             += tga
FATE_LAVF_IMAGES-$(call ENCDEC,  TIFF,           IMAGE2)             += tiff
FATE_LAVF_IMAGES-$(call ENCDEC,  XWD,            IMAGE2)             += xwd

FATE_LAVF_IMAGES = $(FATE_LAVF_IMAGES-yes:%=fate-lavf-%)

$(FATE_LAVF_IMAGES): CMD = lavf_image
$(FATE_LAVF_IMAGES): REF = $(SRC_PATH)/tests/ref/lavf/$(@:fate-lavf-%=%)
$(FATE_LAVF_IMAGES): $(VREF)

fate-lavf-jpg: CMD = lavf_image "-pix_fmt yuvj420p" "-f image2"
fate-lavf-tiff: CMD = lavf_image "-pix_fmt rgb24"

FATE_AVCONV += $(FATE_LAVF_IMAGES)
fate-lavf-images fate-lavf: $(FATE_LAVF_IMAGES)
