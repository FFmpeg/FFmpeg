FATE_OMA_FFMPEG-$(call ALLYES, OMA_DEMUXER CRC_MUXER) += fate-oma-demux
fate-oma-demux: CMD = crc -i $(TARGET_SAMPLES)/oma/01-Untitled-partial.oma -c:a copy

# Also tests splitting and joining the date into TYER and TDAT id3v2.3 tags.
FATE_OMA_REMUX_FFPROBE-yes += fate-oma-atrac3p-remux
fate-oma-atrac3p-remux: CMD = transcode oma $(TARGET_SAMPLES)/atrac3p/at3p_sample1.oma oma "-c copy -metadata date=2021-09-23 -metadata time=16:00 -metadata title=noise -metadata id3v2_priv.foo=hex\xB3 -metadata_header_padding 500" "-c copy -t 0.2" "" "-show_entries format_tags"

FATE_OMA_REMUX-$(CONFIG_WAV_DEMUXER) += fate-oma-atrac3-remux
fate-oma-atrac3-remux: CMD = transcode wav $(TARGET_SAMPLES)/atrac3/mc_sich_at3_132_small.wav oma "-c copy" "-c copy -t 0.1"

FATE_OMA_FFMPEG-$(call ALLYES, FILE_PROTOCOL OMA_MUXER    \
                               OMA_DEMUXER FRAMECRC_MUXER \
                               PIPE_PROTOCOL)             \
                               += $(FATE_OMA_REMUX-yes)
FATE_OMA_FFMPEG_FFPROBE-$(call ALLYES, FILE_PROTOCOL OMA_MUXER    \
                                       OMA_DEMUXER FRAMECRC_MUXER \
                                       PIPE_PROTOCOL)             \
                                       += $(FATE_OMA_REMUX_FFPROBE-yes)
FATE_SAMPLES_FFMPEG         += $(FATE_OMA_FFMPEG-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_OMA_FFMPEG_FFPROBE-yes)
fate-oma: $(FATE_OMA_FFMPEG-yes) $(FATE_OMA_FFMPEG_FFPROBE-yes)
