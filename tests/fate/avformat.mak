FATE_LAVF-$(call ENCDEC,  PCM_S16BE,             AIFF)               += aiff
FATE_LAVF-$(call ENCDEC,  PCM_ALAW,              PCM_ALAW)           += alaw
FATE_LAVF-$(call ENCDEC2, MSMPEG4V3,  MP2,       ASF)                += asf
FATE_LAVF-$(call ENCDEC,  PCM_S16BE_PLANAR,      AST)                += ast
FATE_LAVF-$(call ENCDEC,  PCM_S16BE,             AU)                 += au
FATE_LAVF-$(call ENCDEC2, MPEG4,      MP2,       AVI)                += avi
FATE_LAVF-$(call ENCDEC,  BMP,                   IMAGE2)             += bmp
FATE_LAVF-$(call ENCDEC,  PCM_S16BE,             CAF)                += caf
FATE_LAVF-$(call ENCDEC,  DPX,                   IMAGE2)             += dpx
FATE_LAVF-$(call ENCDEC2, DVVIDEO,    PCM_S16LE, AVI)                += dv_fmt
FATE_LAVF-$(call ENCDEC2, MPEG1VIDEO, MP2,       FFM)                += ffm
FATE_LAVF-$(call ENCDEC,  RAWVIDEO,              FILMSTRIP)          += flm
FATE_LAVF-$(call ENCDEC,  FLV,                   FLV)                += flv_fmt
FATE_LAVF-$(call ENCDEC,  GIF,                   IMAGE2)             += gif
FATE_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, GXF)                += gxf
FATE_LAVF-$(call ENCDEC,  PCM_S16LE,             IRCAM)              += ircam
FATE_LAVF-$(call ENCDEC,  MJPEG,                 IMAGE2)             += jpg
FATE_LAVF-$(call ENCMUX,  TTA,                   MATROSKA_AUDIO)     += mka
FATE_LAVF-$(call ENCDEC2, MPEG4,      MP2,       MATROSKA)           += mkv
FATE_LAVF-$(call ENCDEC,  ADPCM_YAMAHA,          MMF)                += mmf
FATE_LAVF-$(call ENCDEC2, MPEG4,      PCM_ALAW,  MOV)                += mov ismv
FATE_LAVF-$(call ENCDEC2, MPEG1VIDEO, MP2,       MPEG1SYSTEM MPEGPS) += mpg
FATE_LAVF-$(call ENCDEC,  PCM_MULAW,             PCM_MULAW)          += mulaw
FATE_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF)                += mxf
FATE_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF_D10 MXF)        += mxf_d10
FATE_LAVF-$(call ENCDEC2, DNXHD,      PCM_S16LE, MXF_OPATOM MXF)     += mxf_opatom
FATE_LAVF-$(call ENCDEC2, DNXHD,      PCM_S16LE, MXF_OPATOM MXF)     += mxf_opatom_audio
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
FATE_LAVF-$(call ENCMUX,  MJPEG PCM_S16LE,       SMJPEG)             += smjpeg
FATE_LAVF-$(call ENCDEC,  PCM_S16LE,             SOX)                += sox
FATE_LAVF-$(call ENCDEC,  SUNRAST,               IMAGE2)             += sunrast
FATE_LAVF-$(call ENCDEC,  FLV,                   SWF)                += swf
FATE_LAVF-$(call ENCDEC,  TARGA,                 IMAGE2)             += tga
FATE_LAVF-$(call ENCDEC,  TIFF,                  IMAGE2)             += tiff
FATE_LAVF-$(call ENCDEC2, MPEG2VIDEO, MP2,       MPEGTS)             += ts
FATE_LAVF-$(call ENCDEC,  TTA,                   TTA)                += tta
FATE_LAVF-$(call ENCDEC,  PCM_U8,                VOC)                += voc
FATE_LAVF-$(call ENCDEC,  PCM_S16LE,             VOC)                += voc_s16
FATE_LAVF-$(call ENCDEC,  PCM_S16LE,             WAV)                += wav
FATE_LAVF-$(call ENCDEC,  PCM_S16LE,             WAV)                += wav_peak
FATE_LAVF-$(call ENCDEC,  PCM_S16LE,             WAV)                += wav_peak_only
FATE_LAVF-$(call ENCMUX,  PCM_S16LE,             W64)                += w64
FATE_LAVF-$(call ENCDEC,  MP2,                   WTV)                += wtv
FATE_LAVF-$(call ENCDEC,  XBM,                   IMAGE2)             += xbm
FATE_LAVF-$(call ENCDEC,  XWD,                   IMAGE2)             += xwd
FATE_LAVF-$(CONFIG_YUV4MPEGPIPE_MUXER)                               += yuv4mpeg

FATE_LAVF += $(FATE_LAVF-yes:%=fate-lavf-%)
FATE_LAVF_PIXFMT-$(CONFIG_SCALE_FILTER) += fate-lavf-pixfmt
FATE_LAVF += $(FATE_LAVF_PIXFMT-yes)

$(FATE_LAVF): $(AREF) $(VREF)
$(FATE_LAVF): CMD = lavftest
$(FATE_LAVF): CMP =

FATE_AVCONV += $(FATE_LAVF)
fate-lavf:     $(FATE_LAVF)

FATE_LAVF_FATE-$(call ALLYES, MATROSKA_DEMUXER   OGG_MUXER)          += ogg_vp3
FATE_LAVF_FATE-$(call ALLYES, MATROSKA_DEMUXER   OGV_MUXER)          += ogg_vp8
FATE_LAVF_FATE-$(call ALLYES, MOV_DEMUXER        LATM_MUXER)         += latm
FATE_LAVF_FATE-$(call ALLYES, MP3_DEMUXER        MP3_MUXER)          += mp3
FATE_LAVF_FATE-$(call ALLYES, MOV_DEMUXER        MOV_MUXER)          += mov_qtrle_mace6
FATE_LAVF_FATE-$(call ALLYES, AVI_DEMUXER        AVI_MUXER)          += avi_cram

FATE_LAVF_FATE +=  $(FATE_LAVF_FATE-yes:%=fate-lavf-fate-%)
$(FATE_LAVF_FATE): CMD = lavffatetest

FATE_SAMPLES_FFMPEG += $(FATE_LAVF_FATE)
fate-lavf-fate:        $(FATE_LAVF_FATE)
