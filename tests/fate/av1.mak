# Tests that reading and writing with codec libaom-av1 preserves HDR10+ metadata.
FATE_AV1_FFMPEG_FFPROBE-$(call ENCDEC, LIBAOM_AV1 VP9, IVF MATROSKA) += fate-libaom-hdr10-plus
fate-libaom-hdr10-plus: CMD = enc_external $(TARGET_SAMPLES)/mkv/hdr10_plus_vp9_sample.webm ivf "-map 0 -c:v libaom-av1 -cpu-used 8" "-show_frames -show_entries frame=side_data_list -codec:v libaom-av1"

FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_AV1_FFMPEG_FFPROBE-yes)

fate-av1: $(FATE_AV1_FFMPEG_FFPROBE-yes)
