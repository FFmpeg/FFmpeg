FATE_LAVF_IMAGES-$(call ENCDEC,  BMP,            IMAGE2)             += bmp
FATE_LAVF_IMAGES-$(call ENCDEC,  DPX,            IMAGE2)             += dpx
FATE_LAVF_IMAGES-$(call ENCDEC,  DPX,            IMAGE2)             += gbrp10le.dpx
FATE_LAVF_IMAGES-$(call ENCDEC,  DPX,            IMAGE2)             += gbrp12le.dpx
FATE_LAVF_IMAGES-$(call ENCDEC,  DPX,            IMAGE2)             += rgb48le.dpx
FATE_LAVF_IMAGES-$(call ENCDEC,  DPX,            IMAGE2)             += rgb48le_10.dpx
FATE_LAVF_IMAGES-$(call ENCDEC,  DPX,            IMAGE2)             += rgba64le.dpx
FATE_LAVF_IMAGES-$(call ENCDEC,  MJPEG,          IMAGE2)             += jpg
FATE_LAVF_IMAGES-$(call ENCDEC,  PAM,            IMAGE2)             += pam
FATE_LAVF_IMAGES-$(call ENCDEC,  PAM,            IMAGE2)             += rgba.pam
FATE_LAVF_IMAGES-$(call ENCDEC,  PAM,            IMAGE2)             += gray.pam
FATE_LAVF_IMAGES-$(call ENCDEC,  PAM,            IMAGE2)             += gray16be.pam
FATE_LAVF_IMAGES-$(call ENCDEC,  PAM,            IMAGE2)             += rgb48be.pam
FATE_LAVF_IMAGES-$(call ENCDEC,  PAM,            IMAGE2)             += monob.pam
FATE_LAVF_IMAGES-$(call ENCDEC,  PCX,            IMAGE2)             += pcx
FATE_LAVF_IMAGES-$(call ENCDEC,  PGM,            IMAGE2)             += pgm
FATE_LAVF_IMAGES-$(call ENCDEC,  PNG,            IMAGE2)             += png
FATE_LAVF_IMAGES-$(call ENCDEC,  PNG,            IMAGE2)             += gray16be.png
FATE_LAVF_IMAGES-$(call ENCDEC,  PNG,            IMAGE2)             += rgb48be.png
FATE_LAVF_IMAGES-$(call ENCDEC,  PPM,            IMAGE2)             += ppm
FATE_LAVF_IMAGES-$(call ENCDEC,  SGI,            IMAGE2)             += sgi
FATE_LAVF_IMAGES-$(call ENCDEC,  SUNRAST,        IMAGE2)             += sun
FATE_LAVF_IMAGES-$(call ENCDEC,  TARGA,          IMAGE2)             += tga
FATE_LAVF_IMAGES-$(call ENCDEC,  TIFF,           IMAGE2)             += tiff
FATE_LAVF_IMAGES-$(call ENCDEC,  XBM,            IMAGE2)             += xbm
FATE_LAVF_IMAGES-$(call ENCDEC,  XWD,            IMAGE2)             += xwd
FATE_LAVF_IMAGES-$(call ENCDEC,  XWD,            IMAGE2)             += rgba.xwd
FATE_LAVF_IMAGES-$(call ENCDEC,  XWD,            IMAGE2)             += rgb565be.xwd
FATE_LAVF_IMAGES-$(call ENCDEC,  XWD,            IMAGE2)             += rgb555be.xwd
FATE_LAVF_IMAGES-$(call ENCDEC,  XWD,            IMAGE2)             += rgb8.xwd
FATE_LAVF_IMAGES-$(call ENCDEC,  XWD,            IMAGE2)             += rgb4_byte.xwd
FATE_LAVF_IMAGES-$(call ENCDEC,  XWD,            IMAGE2)             += gray.xwd
FATE_LAVF_IMAGES-$(call ENCDEC,  XWD,            IMAGE2)             += monow.xwd

FATE_LAVF_IMAGES = $(FATE_LAVF_IMAGES-yes:%=fate-lavf-%)

$(FATE_LAVF_IMAGES): CMD = lavf_image
$(FATE_LAVF_IMAGES): REF = $(SRC_PATH)/tests/ref/lavf/$(@:fate-lavf-%=%)
$(FATE_LAVF_IMAGES): $(VREF)

fate-lavf-jpg: CMD = lavf_image "-pix_fmt yuvj420p"
fate-lavf-tiff: CMD = lavf_image "-pix_fmt rgb24"
fate-lavf-gbrp10le.dpx: CMD = lavf_image "-pix_fmt gbrp10le" "-pix_fmt gbrp10le"
fate-lavf-gbrp12le.dpx: CMD = lavf_image "-pix_fmt gbrp12le" "-pix_fmt gbrp12le"
fate-lavf-rgb48le.dpx: CMD = lavf_image "-pix_fmt rgb48le"
fate-lavf-rgb48le_10.dpx: CMD = lavf_image "-pix_fmt rgb48le -bits_per_raw_sample 10" "-pix_fmt rgb48le"
fate-lavf-rgba64le.dpx: CMD = lavf_image "-pix_fmt rgba64le"
fate-lavf-rgba.pam: CMD = lavf_image "-pix_fmt rgba"
fate-lavf-gray.pam: CMD = lavf_image "-pix_fmt gray"
fate-lavf-gray16be.pam: CMD = lavf_image "-pix_fmt gray16be" "-pix_fmt gray16be"
fate-lavf-rgb48be.pam: CMD = lavf_image "-pix_fmt rgb48be" "-pix_fmt rgb48be"
fate-lavf-monob.pam: CMD = lavf_image "-pix_fmt monob"
fate-lavf-gray16be.png: CMD = lavf_image "-pix_fmt gray16be"
fate-lavf-rgb48be.png: CMD = lavf_image "-pix_fmt rgb48be"
fate-lavf-rgba.xwd: CMD = lavf_image "-pix_fmt rgba"
fate-lavf-rgb565be.xwd: CMD = lavf_image "-pix_fmt rgb565be"
fate-lavf-rgb555be.xwd: CMD = lavf_image "-pix_fmt rgb555be"
fate-lavf-rgb8.xwd: CMD = lavf_image "-pix_fmt rgb8"
fate-lavf-rgb4_byte.xwd: CMD = lavf_image "-pix_fmt rgb4_byte"
fate-lavf-gray.xwd: CMD = lavf_image "-pix_fmt gray"
fate-lavf-monow.xwd: CMD = lavf_image "-pix_fmt monow"

FATE_AVCONV += $(FATE_LAVF_IMAGES)
fate-lavf-images fate-lavf: $(FATE_LAVF_IMAGES)
