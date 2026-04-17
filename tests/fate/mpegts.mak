#
# Test probing MPEGTS format and codecs
#
PROBE_CODEC_NAME_COMMAND = \
    ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=codec_name \
    -print_format default -bitexact -v 0

FATE_MPEGTS_PROBE-$(call DEMDEC, MPEGTS, HEVC, LOAS_DEMUXER) += fate-mpegts-probe-latm
fate-mpegts-probe-latm: SRC = $(TARGET_SAMPLES)/mpegts/loewe.ts
fate-mpegts-probe-latm: CMD = run $(PROBE_CODEC_NAME_COMMAND) -i "$(SRC)"


FATE_MPEGTS_PROBE-$(call DEMDEC, MPEGTS, HEVC, LOAS_DEMUXER) += fate-mpegts-probe-program
fate-mpegts-probe-program: SRC = $(TARGET_SAMPLES)/mpegts/loewe.ts
fate-mpegts-probe-program: CMD = run $(PROBE_CODEC_NAME_COMMAND) -select_streams p:769:v:0 -i "$(SRC)"


FATE_MPEGTS_PROBE-$(call DEMDEC, MPEGTS) += fate-mpegts-probe-pmt-merge
fate-mpegts-probe-pmt-merge: SRC = $(TARGET_SAMPLES)/mpegts/pmtchange.ts
fate-mpegts-probe-pmt-merge: CMD = run $(PROBE_CODEC_NAME_COMMAND) -merge_pmt_versions 1 -i "$(SRC)"

FATE_MPEGTS_FFMPEG_FFPROBE-$(call FRAMECRC, MOV, H264, H264_PARSER LCEVC_PARSER) += fate-mpegts-lcevc-h264-single-track
fate-mpegts-lcevc-h264-single-track: CMD = stream_demux mpegts $(TARGET_SAMPLES)/lcevc/L_H264_640x360p_8bit8bit_2D_dd.ts \
  "" "-c:v copy" \
  "-show_entries frame_side_data -export_side_data enhancements"

FATE_MPEGTS_FFMPEG_FFPROBE-$(call FRAMECRC, MOV, HEVC, HEVC_PARSER LCEVC_PARSER) += fate-mpegts-lcevc-hevc-single-track
fate-mpegts-lcevc-hevc-single-track: CMD = stream_demux mpegts $(TARGET_SAMPLES)/lcevc/L_HEVC_640x360p_8bit8bit_2D_dd.ts \
  "" "-c:v copy" \
  "-show_entries frame_side_data -export_side_data enhancements"

FATE_MPEGTS_FFMPEG_FFPROBE-$(call FRAMECRC, MOV, VVC, VVC_PARSER LCEVC_PARSER) += fate-mpegts-lcevc-vvc-single-track
fate-mpegts-lcevc-vvc-single-track: CMD = stream_demux mpegts $(TARGET_SAMPLES)/lcevc/L_VVC_640x360p_8bit8bit_2D_dd.ts \
  "" "-c:v copy" \
  "-show_entries frame_side_data -export_side_data enhancements"

FATE_MPEGTS_FFMPEG_FFPROBE-$(call FRAMECRC, MOV, H264, H264_PARSER LCEVC_PARSER) += fate-mpegts-lcevc-h264-dual-track
fate-mpegts-lcevc-h264-dual-track: CMD = stream_demux mpegts $(TARGET_SAMPLES)/lcevc/L_H264_640x360p_8bit8bit_2D_dd_dualTrack.ts \
  "" "-c:v copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream=index,id,codec_name"

FATE_MPEGTS_FFMPEG_FFPROBE-$(call FRAMECRC, MOV, HEVC, HEVC_PARSER LCEVC_PARSER) += fate-mpegts-lcevc-hevc-dual-track
fate-mpegts-lcevc-hevc-dual-track: CMD = stream_demux mpegts $(TARGET_SAMPLES)/lcevc/L_HEVC_640x360p_8bit8bit_2D_dd_dualTrack.ts \
  "" "-c:v copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream=index,id,codec_name"

FATE_MPEGTS_FFMPEG_FFPROBE-$(call FRAMECRC, MOV, VVC, VVC_PARSER LCEVC_PARSER) += fate-mpegts-lcevc-vvc-dual-track
fate-mpegts-lcevc-vvc-dual-track: CMD = stream_demux mpegts $(TARGET_SAMPLES)/lcevc/L_VVC_640x360p_8bit8bit_2D_dd_dualTrack.ts \
  "" "-c:v copy -map 0" \
  "-show_entries stream_group=index,id,nb_streams,type:stream=index,id,codec_name"

FATE_SAMPLES_FFPROBE += $(FATE_MPEGTS_PROBE-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_MPEGTS_FFMPEG_FFPROBE-yes)

fate-mpegts: $(FATE_MPEGTS_PROBE-yes) $(FATE_MPEGTS_FFMPEG_FFPROBE-yes)
