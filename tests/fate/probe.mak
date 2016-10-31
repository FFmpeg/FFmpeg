FATE_PROBE_FORMAT-$(CONFIG_MPEGPS_DEMUXER) += fate-probe-format-roundup997
fate-probe-format-roundup997:  REF = mpeg

FATE_PROBE_FORMAT-$(CONFIG_MP3_DEMUXER) += fate-probe-format-roundup1383
fate-probe-format-roundup1383: REF = mp3

FATE_PROBE_FORMAT-$(CONFIG_MPEGPS_DEMUXER) += fate-probe-format-roundup1414
fate-probe-format-roundup1414: REF = mpeg

FATE_PROBE_FORMAT-$(CONFIG_DV_DEMUXER) += fate-probe-format-roundup2015
fate-probe-format-roundup2015: REF = dv

FATE_PROBE_FORMAT = $(FATE_PROBE_FORMAT-yes)

FATE_EXTERN-$(CONFIG_FFPROBE) += $(FATE_PROBE_FORMAT)
fate-probe-format: $(FATE_PROBE_FORMAT)

$(FATE_PROBE_FORMAT): ffprobe$(PROGSSUF)$(EXESUF)
$(FATE_PROBE_FORMAT): CMP = oneline
fate-probe-format-%: CMD = probefmt $(TARGET_SAMPLES)/probe-format/$(@:fate-probe-format-%=%)
