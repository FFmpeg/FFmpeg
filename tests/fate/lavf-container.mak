FATE_LAVF_CONTAINER-$(call ENCDEC2, MSMPEG4V3,  MP2,       ASF)                += asf
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG4,      MP2,       AVI)                += avi
FATE_LAVF_CONTAINER-$(call ENCDEC2, DVVIDEO,    PCM_S16LE, AVI)                += dv dv_pal dv_ntsc
FATE_LAVF_CONTAINER-$(call ENCDEC,  FLV,                   FLV)                += flv
FATE_LAVF_CONTAINER-$(call ENCDEC,  RAWVIDEO,              FILMSTRIP)          += flm
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, GXF)                += gxf gxf_pal gxf_ntsc
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG4,      MP2,       MATROSKA)           += mkv mkv_attachment
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG4,      PCM_ALAW,  MOV)                += mov mov_rtphint ismv
FATE_LAVF_CONTAINER-$(call ENCDEC,  MPEG4,                 MOV)                += mp4
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG1VIDEO, MP2,       MPEG1SYSTEM MPEGPS) += mpg
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF)                += mxf mxf_dv25 mxf_dvcpro50
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF_D10 MXF)        += mxf_d10
FATE_LAVF_CONTAINER-$(call ENCDEC2, DNXHD,      PCM_S16LE, MXF_OPATOM MXF)     += mxf_opatom mxf_opatom_audio
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG4,      MP2,       NUT)                += nut
FATE_LAVF_CONTAINER-$(call ENCMUX,  RV10 AC3_FIXED,        RM)                 += rm
FATE_LAVF_CONTAINER-$(call ENCMUX,  MJPEG PCM_S16LE,       SMJPEG)             += smjpeg
FATE_LAVF_CONTAINER-$(call ENCDEC,  FLV,                   SWF)                += swf
FATE_LAVF_CONTAINER-$(call ENCDEC2, MPEG2VIDEO, MP2,       MPEGTS)             += ts
FATE_LAVF_CONTAINER-$(call ENCDEC,  MP2,                   WTV)                += wtv

FATE_LAVF_CONTAINER = $(FATE_LAVF_CONTAINER-yes:%=fate-lavf-%)

$(FATE_LAVF_CONTAINER): CMD = lavf_container
$(FATE_LAVF_CONTAINER): REF = $(SRC_PATH)/tests/ref/lavf/$(@:fate-lavf-%=%)
$(FATE_LAVF_CONTAINER): $(AREF) $(VREF)

fate-lavf-asf: CMD = lavf_container "" "-c:a mp2 -ar 44100" "-r 25"
fate-lavf-avi fate-lavf-nut: CMD = lavf_container "" "-c:a mp2 -ar 44100 -threads 1"
fate-lavf-dv:  CMD = lavf_container "-ar 48000 -channel_layout stereo" "-r 25 -s pal"
fate-lavf-dv_pal:  CMD = lavf_container_timecode_nodrop "-ar 48000 -r 25 -s pal -ac 2 -f dv"
fate-lavf-dv_ntsc:  CMD = lavf_container_timecode_drop "-ar 48000 -pix_fmt yuv411p -s ntsc -ac 2 -f dv"
fate-lavf-flv fate-lavf-swf: CMD = lavf_container "" "-an"
fate-lavf-flm: CMD = lavf_container "" "-pix_fmt rgba"
fate-lavf-gxf: CMD = lavf_container "-ar 48000" "-r 25 -s pal -ac 1 -threads 1"
fate-lavf-gxf_pal: CMD = lavf_container_timecode_nodrop "-ar 48000 -r 25 -s pal -ac 1 -threads 1 -f gxf"
fate-lavf-gxf_ntsc: CMD = lavf_container_timecode_drop "-ar 48000 -s ntsc -ac 1 -threads 1 -f gxf"
fate-lavf-ismv: CMD = lavf_container_timecode "-an -write_tmcd 1 -c:v mpeg4 -threads 1"
fate-lavf-mkv: CMD = lavf_container "" "-c:a mp2 -c:v mpeg4 -ar 44100 -threads 1"
fate-lavf-mkv_attachment: CMD = lavf_container_attach "-c:a mp2 -c:v mpeg4 -threads 1 -f matroska"
fate-lavf-mov: CMD = lavf_container_timecode "-movflags +faststart -c:a pcm_alaw -c:v mpeg4 -threads 1"
fate-lavf-mov_rtphint: CMD = lavf_container "" "-movflags +rtphint -c:a pcm_alaw -c:v mpeg4 -threads 1 -f mov"
fate-lavf-mp4: CMD = lavf_container_timecode "-c:v mpeg4 -an -threads 1"
fate-lavf-mpg: CMD = lavf_container_timecode "-ar 44100 -threads 1"
fate-lavf-mxf: CMD = lavf_container_timecode "-ar 48000 -bf 2 -threads 1"
fate-lavf-mxf_d10: CMD = lavf_container "-ar 48000 -ac 2" "-r 25 -vf scale=720:576,pad=720:608:0:32 -c:v mpeg2video -g 0 -flags +ildct+low_delay -dc 10 -non_linear_quant 1 -intra_vlc 1 -qscale 1 -ps 1 -qmin 1 -rc_max_vbv_use 1 -rc_min_vbv_use 1 -pix_fmt yuv422p -minrate 30000k -maxrate 30000k -b 30000k -bufsize 1200000 -top 1 -rc_init_occupancy 1200000 -qmax 12 -f mxf_d10"
fate-lavf-mxf_dv25: CMD = lavf_container "-ar 48000 -ac 2" "-r 25 -vf scale=720:576,setdar=4/3 -c:v dvvideo -pix_fmt yuv420p -b 25000k -top 0 -f mxf"
fate-lavf-mxf_dvcpro50: CMD = lavf_container "-ar 48000 -ac 2" "-r 25 -vf scale=720:576,setdar=16/9 -c:v dvvideo -pix_fmt yuv422p -b 50000k -top 0 -f mxf"
fate-lavf-mxf_opatom: CMD = lavf_container "" "-s 1920x1080 -c:v dnxhd -pix_fmt yuv422p -vb 36M -f mxf_opatom -map 0"
fate-lavf-mxf_opatom_audio: CMD = lavf_container "-ar 48000 -ac 1" "-f mxf_opatom -mxf_audio_edit_rate 25 -map 1"
fate-lavf-smjpeg:  CMD = lavf_container "" "-f smjpeg"
# The RealMedia muxer is broken.
fate-lavf-rm:  CMD = lavf_container "" "-c:a ac3_fixed" disable_crc
fate-lavf-ts:  CMD = lavf_container "" "-mpegts_transport_stream_id 42 -ar 44100 -threads 1"
fate-lavf-wtv: CMD = lavf_container "" "-c:a mp2 -threads 1"

FATE_AVCONV += $(FATE_LAVF_CONTAINER)
fate-lavf-container fate-lavf: $(FATE_LAVF_CONTAINER)

FATE_LAVF_CONTAINER_FATE-$(call ALLYES, IVF_DEMUXER AV1_PARSER MOV_MUXER)      += av1.mp4
FATE_LAVF_CONTAINER_FATE-$(call ALLYES, IVF_DEMUXER AV1_PARSER MATROSKA_MUXER) += av1.mkv
FATE_LAVF_CONTAINER_FATE-$(call ALLYES, H264_DEMUXER H264_PARSER MOV_MUXER)    += h264.mp4
FATE_LAVF_CONTAINER_FATE-$(call ALLYES, MATROSKA_DEMUXER   OGG_MUXER)          += vp3.ogg
FATE_LAVF_CONTAINER_FATE-$(call ALLYES, MATROSKA_DEMUXER   OGV_MUXER)          += vp8.ogg
FATE_LAVF_CONTAINER_FATE-$(call ALLYES, MOV_DEMUXER        LATM_MUXER)         += latm
FATE_LAVF_CONTAINER_FATE-$(call ALLYES, MP3_DEMUXER        MP3_MUXER)          += mp3
FATE_LAVF_CONTAINER_FATE-$(call ALLYES, MOV_DEMUXER        MOV_MUXER)          += qtrle_mace6.mov
FATE_LAVF_CONTAINER_FATE-$(call ALLYES, AVI_DEMUXER        AVI_MUXER)          += cram.avi

FATE_LAVF_CONTAINER_FATE = $(FATE_LAVF_CONTAINER_FATE-yes:%=fate-lavf-fate-%)

$(FATE_LAVF_CONTAINER_FATE): REF = $(SRC_PATH)/tests/ref/lavf-fate/$(@:fate-lavf-fate-%=%)
$(FATE_LAVF_CONTAINER_FATE): $(AREF) $(VREF)

fate-lavf-fate-av1.mp4: CMD = lavf_container_fate "av1-test-vectors/av1-1-b8-05-mv.ivf" "" "-c:v copy"
fate-lavf-fate-av1.mkv: CMD = lavf_container_fate "av1-test-vectors/av1-1-b8-05-mv.ivf" "" "-c:v copy"
fate-lavf-fate-h264.mp4: CMD = lavf_container_fate "h264/intra_refresh.h264" "" "-c:v copy"
fate-lavf-fate-vp3.ogg: CMD = lavf_container_fate "vp3/coeff_level64.mkv" "-idct auto"
fate-lavf-fate-vp8.ogg: CMD = lavf_container_fate "vp8/RRSF49-short.webm" "" "-acodec copy"
fate-lavf-fate-latm: CMD = lavf_container_fate "aac/al04_44.mp4" "" "-acodec copy"
fate-lavf-fate-mp3: CMD = lavf_container_fate "mp3-conformance/he_32khz.bit" "" "-acodec copy"
fate-lavf-fate-qtrle_mace6.mov: CMD = lavf_container_fate "qtrle/Animation-16Greys.mov" "-idct auto"
fate-lavf-fate-cram.avi: CMD = lavf_container_fate "cram/toon.avi" "-idct auto"

FATE_SAMPLES_FFMPEG += $(FATE_LAVF_CONTAINER_FATE)
fate-lavf-fate fate-lavf: $(FATE_LAVF_CONTAINER_FATE)
