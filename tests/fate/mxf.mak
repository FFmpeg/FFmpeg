
FATE_MXF += fate-mxf-missing-index-demux
fate-mxf-missing-index-demux: CMD = crc -i $(TARGET_SAMPLES)/mxf/opatom_missing_index.mxf -acodec copy

FATE_MXF += fate-mxf-essencegroup-demux
fate-mxf-essencegroup-demux: CMD = framecrc -i $(TARGET_SAMPLES)/mxf/opatom_essencegroup_alpha_raw.mxf -vcodec copy

FATE_MXF += fate-mxf-multiple-components-demux
fate-mxf-multiple-components-demux: CMD = framecrc -i $(TARGET_SAMPLES)/mxf/multiple_components.mxf -vcodec copy

FATE_MXF += fate-mxf-metadata-source-ref1
fate-mxf-metadata-source-ref1: CMD = fmtstdout ffmetadata -i $(TARGET_SAMPLES)/mxf/track_01_v02.mxf -fflags +bitexact -flags +bitexact -map 0:0 -map 0:2 -map 0:3  -map_metadata:g -1

FATE_MXF += fate-mxf-metadata-source-ref2
fate-mxf-metadata-source-ref2: CMD = fmtstdout ffmetadata -i $(TARGET_SAMPLES)/mxf/track_02_a01.mxf -fflags +bitexact -flags +bitexact -map 0:0 -map 0:1 -map 0:3  -map_metadata:g -1

FATE_MXF-$(CONFIG_MXF_DEMUXER) += $(FATE_MXF)

FATE_SAMPLES_AVCONV += $(FATE_MXF-yes)
fate-mxf: $(FATE_MXF-yes)
