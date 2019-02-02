FATE_LAVF_CONTAINER-$(call ENCDEC2, MSMPEG4V3,  MP2,       ASF)                += asf
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG4,      MP2,       AVI)                += avi
FATE_LAVF_CONTAINER-$(call ENCDEC2, DVVIDEO,    PCM_S16LE, AVI)                += dv
FATE_LAVF_CONTAINER-$(call ENCDEC,  FLV,                   FLV)                += flv
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, GXF)                += gxf
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG4,      MP2,       MATROSKA)           += mkv
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG4,      PCM_ALAW,  MOV)                += mov
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG1VIDEO, MP2,       MPEG1SYSTEM MPEGPS) += mpg
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF)                += mxf
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF_D10 MXF)        += mxf_d10
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG4,      MP2,       NUT)                += nut
FATE_LAVF_CONTAINER-$(call ENCMUX,  RV10 AC3_FIXED,        RM)                 += rm
FATE_LAVF_CONTAINER-$(call ENCDEC,  FLV,                   SWF)                += swf
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG2VIDEO, MP2,       MPEGTS)             += ts

FATE_LAVF_CONTAINER = $(FATE_LAVF_CONTAINER-yes:%=fate-lavf-%)

$(FATE_LAVF_CONTAINER): CMD = lavf_container
$(FATE_LAVF_CONTAINER): REF = $(SRC_PATH)/tests/ref/lavf/$(@:fate-lavf-%=%)
$(FATE_LAVF_CONTAINER): $(AREF) $(VREF)

fate-lavf-asf: CMD = lavf_container "" "-c:a mp2 -ar 44100" "-r 25"
fate-lavf-avi fate-lavf-nut: CMD = lavf_container "" "-c:a mp2 -ar 44100"
fate-lavf-dv:  CMD = lavf_container "-ar 48000 -channel_layout stereo" "-r 25 -s pal"
fate-lavf-flv fate-lavf-swf: CMD = lavf_container "" "-an"
fate-lavf-gxf: CMD = lavf_container "-ar 48000" "-r 25 -s pal -ac 1"
fate-lavf-mkv: CMD = lavf_container "" "-c:a mp2 -c:v mpeg4 -ar 44100"
fate-lavf-mov: CMD = lavf_container "" "-c:a pcm_alaw -c:v mpeg4"
fate-lavf-mpg: CMD = lavf_container "" "-ar 44100"
fate-lavf-mxf: CMD = lavf_container "-ar 48000" "-bf 2 -timecode_frame_start 264363"
fate-lavf-mxf_d10: CMD = lavf_container "-ar 48000 -ac 2" "-r 25 -vf scale=720:576,pad=720:608:0:32 -c:v mpeg2video -g 0 -flags +ildct+low_delay -dc 10 -non_linear_quant 1 -intra_vlc 1 -qscale 1 -ps 1 -qmin 1 -rc_max_vbv_use 1 -rc_min_vbv_use 1 -pix_fmt yuv422p -minrate 30000k -maxrate 30000k -b 30000k -bufsize 1200000 -top 1 -rc_init_occupancy 1200000 -qmax 12 -f mxf_d10"
# The RealMedia muxer is broken.
fate-lavf-rm:  CMD = lavf_container "" "-c:a ac3_fixed" disable_crc
fate-lavf-ts:  CMD = lavf_container "" "-mpegts_transport_stream_id 42 -ar 44100"

FATE_AVCONV += $(FATE_LAVF_CONTAINER)
fate-lavf-container fate-lavf: $(FATE_LAVF_CONTAINER)
