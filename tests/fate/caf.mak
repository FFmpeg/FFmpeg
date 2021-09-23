FATE_CAF_FFMPEG-$(call ALLYES, CAF_DEMUXER CRC_MUXER) += fate-caf-demux
fate-caf-demux: CMD = crc -i $(TARGET_SAMPLES)/caf/caf-pcm16.caf -c copy

FATE_CAF_REMUX_FFPROBE-$(CONFIG_MOV_DEMUXER) += fate-caf-alac-remux
fate-caf-alac-remux: CMD = transcode m4a $(TARGET_SAMPLES)/lossless-audio/inside.m4a caf "-map 0:a -c copy -metadata major_brand= " "-c copy -t 0.2" "" "-show_entries format_tags"

FATE_CAF_REMUX-$(CONFIG_AMR_DEMUXER) += fate-caf-amr_nb-remux
fate-caf-amr_nb-remux: CMD = transcode amr $(TARGET_SAMPLES)/amrnb/4.75k.amr caf "-c copy" "-c copy -t 0.2"

FATE_CAF_REMUX-$(CONFIG_MOV_DEMUXER) += fate-caf-qdm2-remux
fate-caf-qdm2-remux: CMD = transcode mov $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-B-QDM2.mov caf "-c copy" "-c copy -t 0.2"

FATE_CAF_REMUX-$(CONFIG_WAV_DEMUXER) += fate-caf-pcm_s24le-remux
fate-caf-pcm_s24le-remux: CMD = transcode wav $(TARGET_SAMPLES)/audio-reference/divertimenti_2ch_96kHz_s24.wav caf "-c copy" "-c copy -t 0.05"

FATE_CAF_REMUX-$(call ALLYES, WAV_DEMUXER PCM_S24LE_DECODER \
                              PCM_S24BE_ENCODER)            \
                              += fate-caf-pcm_s24-remux
fate-caf-pcm_s24-remux: CMD = transcode wav $(TARGET_SAMPLES)/audio-reference/divertimenti_2ch_96kHz_s24.wav caf "-c pcm_s24be" "-c copy -t 0.05"

FATE_CAF_REMUX-$(CONFIG_MOV_DEMUXER) += fate-caf-mace6-remux
fate-caf-mace6-remux: CMD = transcode mov $(TARGET_SAMPLES)/qtrle/Animation-16Greys.mov caf "-map 0:a -c copy" "-c copy -t 0.003"

FATE_CAF_FFMPEG-$(call ALLYES, FILE_PROTOCOL CAF_MUXER CAF_DEMUXER \
                               FRAMECRC_MUXER PIPE_PROTOCOL)       \
                               += $(FATE_CAF_REMUX-yes)
FATE_CAF_FFMPEG_FFPROBE-$(call ALLYES, FILE_PROTOCOL CAF_MUXER    \
                                       CAF_DEMUXER FRAMECRC_MUXER \
                                       PIPE_PROTOCOL)             \
                                      += $(FATE_CAF_REMUX_FFPROBE-yes)
FATE_SAMPLES_FFMPEG         += $(FATE_CAF_FFMPEG-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_CAF_FFMPEG_FFPROBE-yes)
fate-caf: $(FATE_CAF_FFMPEG-yes) $(FATE_CAF_FFMPEG_FFPROBE-yes)
