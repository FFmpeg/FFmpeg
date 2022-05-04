
FATE_MXF += fate-mxf-missing-index-demux
fate-mxf-missing-index-demux: CMD = crc -i $(TARGET_SAMPLES)/mxf/opatom_missing_index.mxf -c:a copy

FATE_MXF += fate-mxf-essencegroup-demux
fate-mxf-essencegroup-demux: CMD = framecrc -i $(TARGET_SAMPLES)/mxf/opatom_essencegroup_alpha_raw.mxf -c:v copy

FATE_MXF += fate-mxf-multiple-components-demux
fate-mxf-multiple-components-demux: CMD = framecrc -i $(TARGET_SAMPLES)/mxf/multiple_components.mxf -c:v copy

FATE_MXF += fate-mxf-metadata-source-ref1
fate-mxf-metadata-source-ref1: CMD = fmtstdout ffmetadata -i $(TARGET_SAMPLES)/mxf/track_01_v02.mxf -fflags +bitexact -flags +bitexact -map 0:0 -map 0:2 -map 0:3  -map_metadata:g -1

FATE_MXF += fate-mxf-metadata-source-ref2
fate-mxf-metadata-source-ref2: CMD = fmtstdout ffmetadata -i $(TARGET_SAMPLES)/mxf/track_02_a01.mxf -fflags +bitexact -flags +bitexact -map 0:0 -map 0:1 -map 0:3  -map_metadata:g -1

#
# Tests probing MXF format and stream properties
#
PROBE_FORMAT_STREAMS_COMMAND = \
    ffprobe$(PROGSSUF)$(EXESUF) -show_entries format=format_name,duration,bit_rate:format_tags:streams:stream_tags \
    -print_format default -bitexact -v 0

FATE_MXF_PROBE-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF) += fate-mxf-probe-d10
fate-mxf-probe-d10: SRC = $(TARGET_SAMPLES)/mxf/Sony-00001.mxf
fate-mxf-probe-d10: CMD = run $(PROBE_FORMAT_STREAMS_COMMAND) -i "$(SRC)"

FATE_MXF_PROBE-$(call ENCDEC, DNXHD, MXF) += fate-mxf-probe-dnxhd
fate-mxf-probe-dnxhd: SRC = $(TARGET_SAMPLES)/mxf/multiple_components.mxf
fate-mxf-probe-dnxhd: CMD = run $(PROBE_FORMAT_STREAMS_COMMAND) -i "$(SRC)"

FATE_MXF_PROBE-$(call ENCDEC2, DVVIDEO, PCM_S16LE, MXF) += fate-mxf-probe-dv25
fate-mxf-probe-dv25: SRC = $(TARGET_SAMPLES)/mxf/Avid-00005.mxf
fate-mxf-probe-dv25: CMD = run $(PROBE_FORMAT_STREAMS_COMMAND) -i "$(SRC)"

FATE_MXF_PROBE-$(call ENCDEC2, PRORES, PCM_S24LE, MXF) += fate-mxf-probe-applehdr10
fate-mxf-probe-applehdr10: SRC = $(TARGET_SAMPLES)/mxf/Meridian-Apple_ProResProxy-HDR10.mxf
fate-mxf-probe-applehdr10: CMD = run $(PROBE_FORMAT_STREAMS_COMMAND) -i "$(SRC)" | sed -e "s/yuv422p10../yuv422p10/"

FATE_MXF_REEL_NAME-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF) += fate-mxf-reel_name
fate-mxf-reel_name: $(SAMPLES)/mxf/Sony-00001.mxf
fate-mxf-reel_name: CMD = md5 -y -i $(TARGET_SAMPLES)/mxf/Sony-00001.mxf  -c copy -timecode 00:00:00:00 -metadata "reel_name=test_reel" -fflags +bitexact -f mxf

FATE_MXF_USER_COMMENTS-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF) += fate-mxf-user-comments
fate-mxf-user-comments: $(SAMPLES)/mxf/Sony-00001.mxf
fate-mxf-user-comments: CMD = md5 -y -i $(TARGET_SAMPLES)/mxf/Sony-00001.mxf -c copy -metadata "comment_test=value" -fflags +bitexact -f mxf

FATE_MXF_D10_USER_COMMENTS-$(call ALLYES, FILE_PROTOCOL MXF_DEMUXER DVVIDEO_DECODER SCALE_FILTER MPEG2VIDEO_ENCODER MXF_D10_MUXER EXTRACT_EXTRADATA_BSF MPEGVIDEO_PARSER PIPE_PROTOCOL FRAMECRC_MUXER) += fate-mxf-d10-user-comments
fate-mxf-d10-user-comments: CMD = transcode mxf $(TARGET_SAMPLES)/mxf/Avid-00005.mxf mxf_d10 "-c:v mpeg2video -b:v 30000k -minrate:v 30000k -maxrate:v 30000k -bufsize:v 30000k -rc_init_occupancy 30000k -vf scale=w=1280:h=720 -an -metadata comment_test=value -metadata company_name=FATE-company -metadata product_name=FATE-test -metadata product_version=3.14159 -store_user_comments 1" "-c copy -frames:v 5" "-show_entries format_tags"

FATE_MXF_OPATOM_USER_COMMENTS-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF) += fate-mxf-opatom-user-comments
fate-mxf-opatom-user-comments: $(SAMPLES)/mxf/Sony-00001.mxf
fate-mxf-opatom-user-comments: CMD = md5 -y -i $(TARGET_SAMPLES)/mxf/Sony-00001.mxf -an -vcodec copy -metadata "comment_test=value" -fflags +bitexact -f mxf_opatom

FATE_MXF-$(CONFIG_MXF_DEMUXER) += $(FATE_MXF)

FATE_SAMPLES_AVCONV += $(FATE_MXF-yes) $(FATE_MXF_REEL_NAME-yes)
FATE_SAMPLES_AVCONV += $(FATE_MXF_USER_COMMENTS-yes) $(FATE_MXF_OPATOM_USER_COMMENTS-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_MXF_D10_USER_COMMENTS-yes)
FATE_SAMPLES_FFPROBE += $(FATE_MXF_PROBE-yes)

fate-mxf: $(FATE_MXF-yes) $(FATE_MXF_PROBE-yes) $(FATE_MXF_REEL_NAME-yes) $(FATE_MXF_USER_COMMENTS-yes) $(FATE_MXF_D10_USER_COMMENTS-yes) $(FATE_MXF_OPATOM_USER_COMMENTS-yes)
