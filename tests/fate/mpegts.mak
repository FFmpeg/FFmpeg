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


FATE_SAMPLES_FFPROBE += $(FATE_MPEGTS_PROBE-yes)

fate-mpegts: $(FATE_MPEGTS_PROBE-yes)
