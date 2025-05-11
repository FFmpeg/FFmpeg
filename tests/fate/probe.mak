FATE_PROBE_FORMAT-$(CONFIG_MPEGPS_DEMUXER) += fate-probe-format-roundup997
fate-probe-format-roundup997:  REF = mpeg

FATE_PROBE_FORMAT-$(CONFIG_MP3_DEMUXER) += fate-probe-format-roundup1383
fate-probe-format-roundup1383: REF = mp3

FATE_PROBE_FORMAT-$(CONFIG_MPEGPS_DEMUXER) += fate-probe-format-roundup1414
fate-probe-format-roundup1414: REF = mpeg

FATE_PROBE_FORMAT-$(CONFIG_DV_DEMUXER) += fate-probe-format-roundup2015
fate-probe-format-roundup2015: REF = dv

FATE_PROBE_FORMAT-$(CONFIG_WAV_DEMUXER) += fate-probe-format-codec-trac11581
fate-probe-format-codec-trac11581: REF = pcm_s16le

FATE_PROBE_FORMAT-$(call ALLYES, WAV_DEMUXER DTS_DEMUXER) += fate-probe-format-codec-dts-in-wav
fate-probe-format-codec-dts-in-wav: REF = dts

FATE_PROBE_FORMAT = $(FATE_PROBE_FORMAT-yes)

FATE_EXTERN-$(CONFIG_FFPROBE) += $(FATE_PROBE_FORMAT)
fate-probe-format: $(FATE_PROBE_FORMAT)

$(FATE_PROBE_FORMAT): ffprobe$(PROGSSUF)$(EXESUF)
$(FATE_PROBE_FORMAT): CMP = oneline
fate-probe-format-%: CMD = probefmt $(TARGET_SAMPLES)/probe-format/$(@:fate-probe-format-%=%)
fate-probe-format-codec-%: CMD = probecodec $(TARGET_SAMPLES)/probe-format/$(@:fate-probe-format-%=%)
