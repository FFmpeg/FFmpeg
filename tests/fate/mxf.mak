
FATE_MXF += fate-mxf-missing-index-demux
fate-mxf-missing-index-demux: CMD = crc -i $(TARGET_SAMPLES)/mxf/opatom_missing_index.mxf -acodec copy

FATE_MXF += fate-mxf-essencegroup-demux
fate-mxf-essencegroup-demux: CMD = framecrc -i $(TARGET_SAMPLES)/mxf/opatom_essencegroup_alpha_raw.mxf -vcodec copy

FATE_MXF-$(CONFIG_MXF_DEMUXER) += $(FATE_MXF)

FATE_SAMPLES_AVCONV += $(FATE_MXF-yes)
fate-mxf: $(FATE_MXF-yes)
