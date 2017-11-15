#
# Test probing MPEGTS format and codecs
#
PROBE_CODEC_NAME_COMMAND = \
    ffprobe$(PROGSSUF)$(EXESUF) -show_entries stream=codec_name \
    -print_format default -bitexact -v 0

FATE_MPEGTS_PROBE-$(call DEMDEC, MPEGTS, HEVC, AAC_LATM) += fate-mpegts-probe-latm
fate-mpegts-probe-latm: SRC = $(TARGET_SAMPLES)/mpegts/loewe.ts
fate-mpegts-probe-latm: CMD = run $(PROBE_CODEC_NAME_COMMAND) -i "$(SRC)"

FATE_SAMPLES_FFPROBE += $(FATE_MPEGTS_PROBE-yes)

fate-mpegts: $(FATE_MPEGTS_PROBE-yes)
