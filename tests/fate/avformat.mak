FATE_LAVF-$(call ENCDEC2, MSMPEG4V3,  MP2,       ASF)                += asf
FATE_LAVF-$(call ENCDEC2, MPEG4,      MP2,       AVI)                += avi
FATE_LAVF-$(call ENCDEC2, DVVIDEO,    PCM_S16LE, AVI)                += dv_fmt
FATE_LAVF-$(call ENCDEC,  FLV,                   FLV)                += flv_fmt
FATE_LAVF-$(call ENCDEC,  GIF,                   IMAGE2)             += gif
FATE_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, GXF)                += gxf
FATE_LAVF-$(call ENCDEC2, MPEG4,      MP2,       MATROSKA)           += mkv
FATE_LAVF-$(call ENCDEC2, MPEG4,      PCM_ALAW,  MOV)                += mov
FATE_LAVF-$(call ENCDEC2, MPEG1VIDEO, MP2,       MPEG1SYSTEM MPEGPS) += mpg
FATE_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF)                += mxf
FATE_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF_D10 MXF)        += mxf_d10
FATE_LAVF-$(call ENCDEC2, MPEG4,      MP2,       NUT)                += nut
FATE_LAVF-$(call ENCMUX,  RV10 AC3_FIXED,        RM)                 += rm
FATE_LAVF-$(call ENCDEC,  FLV,                   SWF)                += swf
FATE_LAVF-$(call ENCDEC2, MPEG2VIDEO, MP2,       MPEGTS)             += ts
FATE_LAVF-$(CONFIG_YUV4MPEGPIPE_MUXER)                               += yuv4mpeg

FATE_LAVF += $(FATE_LAVF-yes:%=fate-lavf-%)

$(FATE_LAVF): $(AREF) $(VREF)
$(FATE_LAVF): CMD = lavftest

FATE_AVCONV += $(FATE_LAVF)
fate-lavf:     $(FATE_LAVF)
