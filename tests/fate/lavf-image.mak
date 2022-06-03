LAVF_IMAGES = $(call ALLYES, FILE_PROTOCOL IMAGE2_DEMUXER PGMYUV_DECODER \
                             SCALE_FILTER $(1)_ENCODER IMAGE2_MUXER      \
                             $(1)_DECODER RAWVIDEO_ENCODER CRC_MUXER)

FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         BMP) += bmp
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         DPX) += dpx
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         DPX) += gbrp10le.dpx
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         DPX) += gbrp12le.dpx
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         DPX) += rgb48le.dpx
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         DPX) += rgb48le_10.dpx
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         DPX) += rgba64le.dpx
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,       MJPEG) += jpg
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         PAM) += pam
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         PAM) += rgba.pam
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         PAM) += gray.pam
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         PAM) += gray16be.pam
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         PAM) += rgb48be.pam
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         PAM) += monob.pam
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         PCX) += pcx
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         PGM) += pgm
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         PNG) += png
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         PNG) += gray16be.png
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         PNG) += rgb48be.png
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         PPM) += ppm
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         SGI) += sgi
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,     SUNRAST) += sun
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,       TARGA) += tga
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,        TIFF) += tiff
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         QOI) += qoi
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         XBM) += xbm
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         XWD) += xwd
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         XWD) += rgba.xwd
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         XWD) += rgb565be.xwd
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         XWD) += rgb555be.xwd
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         XWD) += rgb8.xwd
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         XWD) += rgb4_byte.xwd
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         XWD) += gray.xwd
FATE_LAVF_IMAGES-$(call LAVF_IMAGES,         XWD) += monow.xwd

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
