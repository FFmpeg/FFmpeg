FATE_ID3V2_FFPROBE-$(CONFIG_MP3_DEMUXER) += fate-id3v2-priv
fate-id3v2-priv: CMD = probetags $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3

FATE_ID3V2_FFMPEG_FFPROBE-$(call ALLYES, FILE_PROTOCOL MP3_DEMUXER MP3_MUXER \
                                         FRAMECRC_MUXER PIPE_PROTOCOL)       \
                            += fate-id3v2-priv-remux
fate-id3v2-priv-remux: CMD = transcode mp3 $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 mp3 "-c copy" "-c copy -t 0.1" "" "-show_entries format_tags"

FATE_SAMPLES_FFPROBE        += $(FATE_ID3V2_FFPROBE-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_ID3V2_FFMPEG_FFPROBE-yes)
fate-id3v2: $(FATE_ID3V2_FFPROBE-yes) $(FATE_ID3V2_FFMPEG_FFPROBE-yes)
