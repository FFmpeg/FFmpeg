FATE_ID3V2_FFPROBE-$(CONFIG_MP3_DEMUXER) += fate-id3v2-priv
fate-id3v2-priv: CMD = probetags $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3

FATE_ID3V2_FFMPEG_FFPROBE-$(call REMUX, MP3) += fate-id3v2-priv-remux
fate-id3v2-priv-remux: CMD = transcode mp3 $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 mp3 "-c copy" "-c copy -t 0.1" "-show_entries format_tags"

FATE_ID3V2_FFMPEG_FFPROBE-$(call REMUX, AIFF, WAV_DEMUXER) += fate-id3v2-chapters
fate-id3v2-chapters: CMD = transcode wav $(TARGET_SAMPLES)/wav/200828-005.wav aiff "-c copy -metadata:c:0 description=foo -metadata:c:0 date=2021 -metadata:c copyright=none -metadata:c:1 genre=nonsense -write_id3v2 1" "-c copy -t 0.05" "-show_entries format_tags:chapters"

FATE_SAMPLES_FFPROBE        += $(FATE_ID3V2_FFPROBE-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_ID3V2_FFMPEG_FFPROBE-yes)
fate-id3v2: $(FATE_ID3V2_FFPROBE-yes) $(FATE_ID3V2_FFMPEG_FFPROBE-yes)
