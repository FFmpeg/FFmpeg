FATE_PROBE_FORMAT += fate-probe-format-roundup997
fate-probe-format-roundup997:  REF = format_name=mpeg

FATE_PROBE_FORMAT += fate-probe-format-roundup1383
fate-probe-format-roundup1383: REF = format_name=mp3

FATE_PROBE_FORMAT += fate-probe-format-roundup1414
fate-probe-format-roundup1414: REF = format_name=mpeg

FATE_PROBE_FORMAT += fate-probe-format-roundup2015
fate-probe-format-roundup2015: REF = format_name=dv

FATE-$(CONFIG_FFPROBE) += $(FATE_PROBE_FORMAT)
fate-probe-format: $(FATE_PROBE_FORMAT)

$(FATE_PROBE_FORMAT): ffprobe$(EXESUF)
$(FATE_PROBE_FORMAT): CMP = oneline
fate-probe-format-%: CMD = probefmt $(SAMPLES)/probe-format/$(@:fate-probe-format-%=%)
