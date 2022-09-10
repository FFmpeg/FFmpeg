FATE_ID3V2_FFPROBE-$(CONFIG_MP3_DEMUXER) += fate-id3v2-priv
fate-id3v2-priv: CMD = probetags $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3

FATE_ID3V2_FFMPEG_FFPROBE-$(call REMUX, MP3) += fate-id3v2-priv-remux
fate-id3v2-priv-remux: CMD = transcode mp3 $(TARGET_SAMPLES)/id3v2/id3v2_priv.mp3 mp3 "-c copy" "-c copy -t 0.1" "-show_entries format_tags"

FATE_ID3V2_FFMPEG_FFPROBE-$(call REMUX, AIFF, WAV_DEMUXER) += fate-id3v2-chapters
fate-id3v2-chapters: CMD = transcode wav $(TARGET_SAMPLES)/wav/200828-005.wav aiff "-c copy -metadata:c:0 description=foo -metadata:c:0 date=2021 -metadata:c copyright=none -metadata:c:1 genre=nonsense -write_id3v2 1" "-c copy -t 0.05" "-show_entries format_tags:chapters"

# Tests reading and writing UTF-16 BOM strings; also tests
# the AIFF muxer's and demuxer's ability to preserve channel layouts.
FATE_ID3V2_FFMPEG_FFPROBE-$(call REMUX, AIFF, WAV_DEMUXER FLAC_DEMUXER PCM_S16LE_DECODER MJPEG_DECODER ARESAMPLE_FILTER CHANNELMAP_FILTER PCM_S24BE_ENCODER) += fate-id3v2-utf16-bom
fate-id3v2-utf16-bom: CMD = transcode wav $(TARGET_SAMPLES)/audio-reference/yo.raw-short.wav aiff "-map 0:a -map 1:v -af aresample,channelmap=channel_layout=hexagonal,aresample -c:a pcm_s24be -c:v copy -write_id3v2 1 -id3v2_version 3 -map_metadata:g:0 1:g -map_metadata:s:v 1:g" "-c copy -t 0.05" "-show_entries stream=channel_layout:stream_tags:format_tags" "-i $(TARGET_SAMPLES)/cover_art/cover_art.flac"

FATE_SAMPLES_FFPROBE        += $(FATE_ID3V2_FFPROBE-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_ID3V2_FFMPEG_FFPROBE-yes)
fate-id3v2: $(FATE_ID3V2_FFPROBE-yes) $(FATE_ID3V2_FFMPEG_FFPROBE-yes)
