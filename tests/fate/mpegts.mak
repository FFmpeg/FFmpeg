#
# Test probing MPEGTS format and codecs
#
PROBE_CODEC_NAME_COMMAND = \
    ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=codec_name \
    -print_format default -bitexact -v 0

FATE_MPEGTS_PROBE-$(call DEMDEC, MPEGTS, HEVC, LOAS_DEMUXER) += fate-mpegts-probe-latm
fate-mpegts-probe-latm: SRC = $(TARGET_SAMPLES)/mpegts/loewe.ts
fate-mpegts-probe-latm: CMD = run $(PROBE_CODEC_NAME_COMMAND) -i "$(SRC)"

FATE_MPEGTS_PROBE-$(call DEMDEC, MPEGTS, HEVC, LOAS_DEMUXER) += fate-mpegts-probe-show-program
fate-mpegts-probe-show-program: SRC = $(TARGET_SAMPLES)/mpegts/loewe.ts
fate-mpegts-probe-show-program: CMD = run $(PROBE_CODEC_NAME_COMMAND) -show_programs \
    -show_entries program=:program_tags=:program_stream_disposition=:program_stream_tags=:stream=codec_name -i "$(SRC)"

FATE_MPEGTS_PROBE-$(call DEMDEC, MPEGTS, HEVC, LOAS_DEMUXER) += fate-mpegts-probe-program-stream
fate-mpegts-probe-program-stream: SRC = $(TARGET_SAMPLES)/mpegts/loewe.ts
fate-mpegts-probe-program-stream: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -v 0 -bitexact \
    -select_streams p:769:v:0 -show_entries program_stream=codec_name -i "$(SRC)"


FATE_MPEGTS_PROBE-$(call DEMDEC, MPEGTS, HEVC, LOAS_DEMUXER) += fate-mpegts-probe-program
fate-mpegts-probe-program: SRC = $(TARGET_SAMPLES)/mpegts/loewe.ts
fate-mpegts-probe-program: CMD = run $(PROBE_CODEC_NAME_COMMAND) -select_streams p:769:v:0 -i "$(SRC)"


FATE_MPEGTS_PROBE-$(call DEMDEC, MPEGTS) += fate-mpegts-probe-pmt-merge
fate-mpegts-probe-pmt-merge: SRC = $(TARGET_SAMPLES)/mpegts/pmtchange.ts
fate-mpegts-probe-pmt-merge: CMD = run $(PROBE_CODEC_NAME_COMMAND) -merge_pmt_versions 1 -i "$(SRC)"

FATE_MPEGTS_PROBE-$(call DEMDEC, MPEGTS, HEVC, LOAS_DEMUXER) += fate-mpegts-probe-program-compact
fate-mpegts-probe-program-compact: SRC = $(TARGET_SAMPLES)/mpegts/loewe.ts
fate-mpegts-probe-program-compact: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -v 0 -bitexact -of compact \
    -show_entries packet=stream_index,pts:packet_side_data_list:program=program_id,nb_streams:program_stream=index,codec_name \
    -read_intervals "%+\#2" -i "$(SRC)"

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
  "" "-c:v copy -map 0:1" \
  "-show_entries stream_group=index,id,nb_streams,type:stream=index,id,codec_name"

FATE_MPEGTS_FFMPEG_FFPROBE-$(call FRAMECRC, MOV, HEVC, HEVC_PARSER LCEVC_PARSER) += fate-mpegts-lcevc-hevc-dual-track
fate-mpegts-lcevc-hevc-dual-track: CMD = stream_demux mpegts $(TARGET_SAMPLES)/lcevc/L_HEVC_640x360p_8bit8bit_2D_dd_dualTrack.ts \
  "" "-c:v copy -map 0:1" \
  "-show_entries stream_group=index,id,nb_streams,type:stream=index,id,codec_name"

FATE_MPEGTS_FFMPEG_FFPROBE-$(call FRAMECRC, MOV, VVC, VVC_PARSER LCEVC_PARSER) += fate-mpegts-lcevc-vvc-dual-track
fate-mpegts-lcevc-vvc-dual-track: CMD = stream_demux mpegts $(TARGET_SAMPLES)/lcevc/L_VVC_640x360p_8bit8bit_2D_dd_dualTrack.ts \
  "" "-c:v copy -map 0:1" \
  "-show_entries stream_group=index,id,nb_streams,type:stream=index,id,codec_name"

# The subtitling descriptor must keep the type of a stream for the hard of
# hearing (0x2x) when the encoder supplies the type itself, and must not
# touch the types of a record holding several subtitle services.
FATE_MPEGTS_FFMPEG_FFPROBE-$(call FRAMECRC, MPEGTS, DVBSUB, DVBSUB_ENCODER DVBSUB_PARSER MPEGTS_MUXER) += fate-mpegts-dvbsub-hearing-impaired
fate-mpegts-dvbsub-hearing-impaired: CMD = transcode mpegts $(TARGET_SAMPLES)/sub/dvbsubtest_filter.ts mpegts \
  "-map 0:s -c dvbsub -disposition:s:0 hearing_impaired" "-map 0:s -c copy" \
  "-show_entries stream_disposition=hearing_impaired -of default=nw=1:nk=1"

FATE_MPEGTS_FFMPEG_FFPROBE-$(call ALLYES, MPEGTS_MUXER MPEGTS_DEMUXER DVBSUB_DECODER DVBSUB_PARSER FILE_PROTOCOL) += fate-mpegts-dvbsub-multi-service
fate-mpegts-dvbsub-multi-service: CMD = \
  cleanfiles="$${cleanfiles} $$(target_path tests/data/fate/mpegts-dvbsub-multi-service.src.ts) \
  $$(target_path tests/data/fate/mpegts-dvbsub-multi-service.ts)"; \
  ffmpeg -i $(TARGET_SAMPLES)/sub/dvbsubtest_filter.ts -map 0:s -c copy \
  -metadata:s:0 language=eng,deu -bitexact -f mpegts -y \
  "$$(target_path tests/data/fate/mpegts-dvbsub-multi-service.src.ts)" && \
  ffmpeg -i "$$(target_path tests/data/fate/mpegts-dvbsub-multi-service.src.ts)" \
  -map 0:s -c copy -disposition:s:0 hearing_impaired -bitexact -f mpegts -y \
  "$$(target_path tests/data/fate/mpegts-dvbsub-multi-service.ts)" && \
  probe -show_entries stream_disposition=hearing_impaired -of default=nw=1:nk=1 \
  "$$(target_path tests/data/fate/mpegts-dvbsub-multi-service.ts)"; \
  test $$? -eq 0

FATE_SAMPLES_FFPROBE += $(FATE_MPEGTS_PROBE-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_MPEGTS_FFMPEG_FFPROBE-yes)

fate-mpegts: $(FATE_MPEGTS_PROBE-yes) $(FATE_MPEGTS_FFMPEG_FFPROBE-yes)
