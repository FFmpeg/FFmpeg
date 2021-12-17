tests/data/mp4-to-ts.m3u8: TAG = GEN
tests/data/mp4-to-ts.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -i $(TARGET_SAMPLES)/h264/interlaced_crop.mp4 \
        -f ssegment -segment_time 1 -map 0 -flags +bitexact -codec copy \
        -segment_list $(TARGET_PATH)/$@ -y $(TARGET_PATH)/tests/data/mp4-to-ts-%03d.ts 2>/dev/null

tests/data/adts-to-mkv.m3u8: TAG = GEN
tests/data/adts-to-mkv.m3u8: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -i $(TARGET_SAMPLES)/audiomatch/tones_afconvert_16000_mono_aac_lc.m4a \
        -f segment -segment_time 1 -map 0 -flags +bitexact -codec copy -segment_format_options live=1 \
        -segment_list $(TARGET_PATH)/$@ -y $(TARGET_PATH)/tests/data/adts-to-mkv-%03d.mkv 2>/dev/null

tests/data/adts-to-mkv-header.mkv: TAG = GEN
tests/data/adts-to-mkv-header.mkv: ffmpeg$(PROGSSUF)$(EXESUF) | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
        -i $(TARGET_SAMPLES)/audiomatch/tones_afconvert_16000_mono_aac_lc.m4a \
        -f segment -segment_time 1 -map 0 -flags +bitexact -codec copy -segment_format_options live=1 \
        -segment_header_filename $(TARGET_PATH)/tests/data/adts-to-mkv-header.mkv \
        -y $(TARGET_PATH)/tests/data/adts-to-mkv-header-%03d.mkv 2>/dev/null

tests/data/adts-to-mkv-header-%.mkv: tests/data/adts-to-mkv-header.mkv ;

FATE_SEGMENT_PARTS += 000 001 002

tests/data/adts-to-mkv-cated-all.mkv: TAG = GEN
tests/data/adts-to-mkv-cated-all.mkv: tests/data/adts-to-mkv-header.mkv $(FATE_SEGMENT_PARTS:%=tests/data/adts-to-mkv-header-%.mkv) | tests/data
	$(M)cat $^ >$@

tests/data/adts-to-mkv-cated-%.mkv: TAG = GEN
tests/data/adts-to-mkv-cated-%.mkv: tests/data/adts-to-mkv-header.mkv tests/data/adts-to-mkv-header-%.mkv | tests/data
	$(M)cat $^ >$@

FATE_SEGMENT += fate-segment-mp4-to-ts
fate-segment-mp4-to-ts: tests/data/mp4-to-ts.m3u8
fate-segment-mp4-to-ts: CMD = framecrc -flags +bitexact -i $(TARGET_PATH)/tests/data/mp4-to-ts.m3u8 -c copy
FATE_SEGMENT-$(call ALLYES, MOV_DEMUXER H264_MP4TOANNEXB_BSF MPEGTS_MUXER MATROSKA_DEMUXER SEGMENT_MUXER HLS_DEMUXER) += fate-segment-mp4-to-ts

FATE_SEGMENT += fate-segment-adts-to-mkv
fate-segment-adts-to-mkv: tests/data/adts-to-mkv.m3u8
fate-segment-adts-to-mkv: CMD = framecrc -flags +bitexact -i $(TARGET_PATH)/tests/data/adts-to-mkv.m3u8 -c copy
fate-segment-adts-to-mkv: REF = $(SRC_PATH)/tests/ref/fate/segment-adts-to-mkv-header-all
FATE_SEGMENT-$(call ALLYES, AAC_DEMUXER AAC_ADTSTOASC_BSF MATROSKA_MUXER MATROSKA_DEMUXER SEGMENT_MUXER HLS_DEMUXER) += fate-segment-adts-to-mkv

FATE_SEGMENT_ALLPARTS = $(FATE_SEGMENT_PARTS)
FATE_SEGMENT_ALLPARTS += all
FATE_SEGMENT_SPLIT += $(FATE_SEGMENT_ALLPARTS:%=fate-segment-adts-to-mkv-header-%)
$(foreach N,$(FATE_SEGMENT_ALLPARTS),$(eval $(N:%=fate-segment-adts-to-mkv-header-%): tests/data/adts-to-mkv-cated-$(N).mkv))
fate-segment-adts-to-mkv-header-%: CMD = framecrc -flags +bitexact -i $(TARGET_PATH)/tests/data/$(@:fate-segment-adts-to-mkv-header-%=adts-to-mkv-cated-%).mkv -c copy
FATE_SEGMENT-$(call ALLYES, AAC_DEMUXER AAC_ADTSTOASC_BSF MATROSKA_MUXER MATROSKA_DEMUXER SEGMENT_MUXER HLS_DEMUXER) += $(FATE_SEGMENT_SPLIT)

FATE_SAMPLES_FFMPEG += $(FATE_SEGMENT-yes)

fate-segment: $(FATE_SEGMENT-yes)
