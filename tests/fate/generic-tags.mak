tests/data/generic-tags.mp3: TAG = GEN
tests/data/generic-tags.mp3: ffmpeg$(PROGSSUF)$(EXESUF) $(SRC_PATH)/tests/generic-tags.ffmeta | tests/data
	$(M)$(TARGET_EXEC) $(TARGET_PATH)/$< -nostdin \
	    -i $(TARGET_SAMPLES)/audiomatch/square3.mp3 \
        -f ffmetadata -i $(SRC_PATH)/tests/generic-tags.ffmeta \
        -c:a copy -map_metadata 1 $(TARGET_PATH)/$@ -y 2>/dev/null

ID3V2_TESTBIN = libavformat/tests/id3v2$(EXESUF)

FATE_GENERIC_TAGS-$(call REMUX, MP3) += fate-generic-tags-remux-mp3
fate-generic-tags-remux-mp3: tests/data/generic-tags.mp3 $(ID3V2_TESTBIN)
fate-generic-tags-remux-mp3: CMD = run_with_temp "$(FFMPEG) -nostdin -hide_banner -loglevel error -i $(TARGET_PATH)/tests/data/generic-tags.mp3 -c copy -fflags +bitexact -f mp3 -y" "$(ID3V2_TESTBIN)" mp3

FATE_GENERIC_TAGS-$(call REMUX, MP3, FLAC_MUXER FLAC_DEMUXER FLAC_ENCODER FLAC_DECODER) += fate-generic-tags-remux-vorbiscomment
fate-generic-tags-remux-vorbiscomment: tests/data/generic-tags.mp3
fate-generic-tags-remux-vorbiscomment: CMD = generic_tags mp3 $(TARGET_PATH)/tests/data/generic-tags.mp3 flac flac "-t 0 -af aresample -strict experimental"

FATE_GENERIC_TAGS-$(call REMUX, MP3, ASF_MUXER ASF_DEMUXER WMAV1_ENCODER WMAV1_DECODER) += fate-generic-tags-remux-asf
fate-generic-tags-remux-asf: tests/data/generic-tags.mp3
fate-generic-tags-remux-asf: CMD = generic_tags mp3 $(TARGET_PATH)/tests/data/generic-tags.mp3 asf wmav1

FATE_GENERIC_TAGS-$(call REMUX, MP3, WAV_MUXER WAV_DEMUXER PCM_S16LE_ENCODER PCM_S16LE_DECODER) += fate-generic-tags-remux-riff
fate-generic-tags-remux-riff: tests/data/generic-tags.mp3
fate-generic-tags-remux-riff: CMD = generic_tags mp3 $(TARGET_PATH)/tests/data/generic-tags.mp3 wav pcm_s16le "-t 0 -af aresample"

FATE_GENERIC_TAGS-$(call REMUX, MP3, MOV_MUXER MOV_DEMUXER AAC_ENCODER AAC_DECODER) += fate-generic-tags-remux-mov
fate-generic-tags-remux-mov: tests/data/generic-tags.mp3
fate-generic-tags-remux-mov: CMD = generic_tags mp3 $(TARGET_PATH)/tests/data/generic-tags.mp3 mp4 aac

FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_GENERIC_TAGS-yes)
fate-generic-tags: $(FATE_GENERIC_TAGS-yes)
