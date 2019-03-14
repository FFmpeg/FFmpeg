FATE_LAVF_VIDEO-$(call ENCDEC,  APNG,       APNG)               += apng
FATE_LAVF_VIDEO-$(call ENCDEC,  APNG,       APNG)               += apng.png
FATE_LAVF_VIDEO-$(call ENCDEC,  FITS,       FITS)               += gray.fits
FATE_LAVF_VIDEO-$(call ENCDEC,  FITS,       FITS)               += gray16be.fits
FATE_LAVF_VIDEO-$(call ENCDEC,  FITS,       FITS)               += gbrp.fits
FATE_LAVF_VIDEO-$(call ENCDEC,  FITS,       FITS)               += gbrap.fits
FATE_LAVF_VIDEO-$(call ENCDEC,  FITS,       FITS)               += gbrp16be.fits
FATE_LAVF_VIDEO-$(call ENCDEC,  FITS,       FITS)               += gbrap16be.fits
FATE_LAVF_VIDEO-$(call ENCDEC,  GIF,        FITS)               += gif
FATE_LAVF_VIDEO-$(CONFIG_YUV4MPEGPIPE_MUXER)                    += y4m

FATE_LAVF_VIDEO = $(FATE_LAVF_VIDEO-yes:%=fate-lavf-%)

$(FATE_LAVF_VIDEO): CMD = lavf_video
$(FATE_LAVF_VIDEO): REF = $(SRC_PATH)/tests/ref/lavf/$(@:fate-lavf-%=%)
$(FATE_LAVF_VIDEO): $(VREF)

fate-lavf-apng: CMD = lavf_video "-pix_fmt rgb24"
fate-lavf-apng.png: CMD = lavf_video "-pix_fmt rgb24" "-frames:v 1 -f apng"
fate-lavf-gray.fits: CMD = lavf_video "-pix_fmt gray"
fate-lavf-gray16be.fits: CMD = lavf_video "-pix_fmt gray16be"
fate-lavf-gbrp.fits: CMD = lavf_video "-pix_fmt gbrp"
fate-lavf-gbrap.fits: CMD = lavf_video "-pix_fmt gbrap"
fate-lavf-gbrp16be.fits: CMD = lavf_video "-pix_fmt gbrp16be"
fate-lavf-gbrap16be.fits: CMD = lavf_video "-pix_fmt gbrap16be"
fate-lavf-gif: CMD = lavf_video "-pix_fmt rgb24"

FATE_AVCONV += $(FATE_LAVF_VIDEO)
fate-lavf-video fate-lavf: $(FATE_LAVF_VIDEO)
