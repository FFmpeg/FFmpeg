FATE_LAVF-$(call ENCDEC,  PCM_S16BE,             AIFF)               += aiff
FATE_LAVF-$(call ENCDEC,  PCM_ALAW,              PCM_ALAW)           += alaw
FATE_LAVF-$(call ENCDEC2, MSMPEG4V3,  MP2,       ASF)                += asf
FATE_LAVF-$(call ENCDEC,  PCM_S16BE,             AU)                 += au
FATE_LAVF-$(call ENCDEC2, MPEG4,      MP2,       AVI)                += avi
FATE_LAVF-$(call ENCDEC,  BMP,                   IMAGE2)             += bmp
FATE_LAVF-$(call ENCDEC,  DPX,                   IMAGE2)             += dpx
FATE_LAVF-$(call ENCDEC2, DVVIDEO,    PCM_S16LE, AVI)                += dv_fmt
FATE_LAVF-$(call ENCDEC,  FLV,                   FLV)                += flv_fmt
FATE_LAVF-$(call ENCDEC,  GIF,                   IMAGE2)             += gif
FATE_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, GXF)                += gxf
FATE_LAVF-$(call ENCDEC,  MJPEG,                 IMAGE2)             += jpg
FATE_LAVF-$(call ENCDEC2, MPEG4,      MP2,       MATROSKA)           += mkv
FATE_LAVF-$(call ENCDEC,  ADPCM_YAMAHA,          MMF)                += mmf
FATE_LAVF-$(call ENCDEC2, MPEG4,      PCM_ALAW,  MOV)                += mov
FATE_LAVF-$(call ENCDEC2, MPEG1VIDEO, MP2,       MPEG1SYSTEM MPEGPS) += mpg
FATE_LAVF-$(call ENCDEC,  PCM_MULAW,             PCM_MULAW)          += mulaw
FATE_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF)                += mxf
FATE_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF_D10 MXF)        += mxf_d10
FATE_LAVF-$(call ENCDEC2, MPEG4,      MP2,       NUT)                += nut
FATE_LAVF-$(call ENCDEC,  FLAC,                  OGG)                += ogg
FATE_LAVF-$(call ENCDEC,  PAM,                   IMAGE2)             += pam
FATE_LAVF-$(call ENCDEC,  PBM,                   IMAGE2PIPE)         += pbmpipe
FATE_LAVF-$(call ENCDEC,  PCX,                   IMAGE2)             += pcx
FATE_LAVF-$(call ENCDEC,  PGM,                   IMAGE2)             += pgm
FATE_LAVF-$(call ENCDEC,  PGM,                   IMAGE2PIPE)         += pgmpipe
FATE_LAVF-$(call ENCDEC,  PNG,                   IMAGE2)             += png
FATE_LAVF-$(call ENCDEC,  PPM,                   IMAGE2)             += ppm
FATE_LAVF-$(call ENCDEC,  PPM,                   IMAGE2PIPE)         += ppmpipe
FATE_LAVF-$(call ENCMUX,  RV10 AC3_FIXED,        RM)                 += rm
FATE_LAVF-$(call ENCDEC,  PCM_U8,                RSO)                += rso
FATE_LAVF-$(call ENCDEC,  SGI,                   IMAGE2)             += sgi
FATE_LAVF-$(call ENCDEC,  PCM_S16LE,             SOX)                += sox
FATE_LAVF-$(call ENCDEC,  SUNRAST,               IMAGE2)             += sunrast
FATE_LAVF-$(call ENCDEC,  FLV,                   SWF)                += swf
FATE_LAVF-$(call ENCDEC,  TARGA,                 IMAGE2)             += tga
FATE_LAVF-$(call ENCDEC,  TIFF,                  IMAGE2)             += tiff
FATE_LAVF-$(call ENCDEC2, MPEG2VIDEO, MP2,       MPEGTS)             += ts
FATE_LAVF-$(call ENCDEC,  PCM_U8,                VOC)                += voc
FATE_LAVF-$(call ENCDEC,  PCM_S16LE,             VOC)                += voc_s16
FATE_LAVF-$(call ENCDEC,  PCM_S16LE,             WAV)                += wav
FATE_LAVF-$(call ENCDEC,  XWD,                   IMAGE2)             += xwd
FATE_LAVF-$(CONFIG_YUV4MPEGPIPE_MUXER)                               += yuv4mpeg

FATE_LAVF += $(FATE_LAVF-yes:%=fate-lavf-%)
FATE_LAVF += fate-lavf-pixfmt

$(FATE_LAVF): $(AREF) $(VREF)
$(FATE_LAVF): CMD = lavftest

FATE_AVCONV += $(FATE_LAVF)
fate-lavf:     $(FATE_LAVF)
