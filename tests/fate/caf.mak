FATE_CAF_FFMPEG-$(call CRC, CAF) += fate-caf-demux
fate-caf-demux: CMD = crc -i $(TARGET_SAMPLES)/caf/caf-pcm16.caf -c copy

FATE_CAF_FFMPEG-$(call DEMMUX, CAF, FRAMECRC, PIPE_PROTOCOL) += fate-caf-ima4-demux
fate-caf-ima4-demux: CMD = framecrc -i $(TARGET_SAMPLES)/caf/ima4.caf -c copy

FATE_CAF_FFMPEG-$(call DEMMUX, CAF, FRAMECRC, PIPE_PROTOCOL) += fate-caf-ilbc-demux
fate-caf-ilbc-demux: CMD = framecrc -i $(TARGET_SAMPLES)/caf/ilbc.caf -c copy

FATE_CAF_FFMPEG-$(call DEMMUX, CAF, FRAMECRC, PIPE_PROTOCOL) += fate-caf-ima4-pipe
fate-caf-ima4-pipe: CMD = cat $(SAMPLES)/caf/ima4.caf | framecrc -i pipe: -c copy
fate-caf-ima4-pipe: REF = $(SRC_PATH)/tests/ref/fate/caf-ima4-demux

FATE_CAF_FFMPEG-$(call DEMMUX, CAF, FRAMECRC, PIPE_PROTOCOL) += fate-caf-ilbc-pipe
fate-caf-ilbc-pipe: CMD = cat $(SAMPLES)/caf/ilbc.caf | framecrc -i pipe: -c copy
fate-caf-ilbc-pipe: REF = $(SRC_PATH)/tests/ref/fate/caf-ilbc-demux

FATE_CAF_FFMPEG_FFPROBE-$(call REMUX, CAF, MOV_DEMUXER) += fate-caf-alac-remux
fate-caf-alac-remux: CMD = transcode m4a $(TARGET_SAMPLES)/lossless-audio/inside.m4a caf "-map 0:a -c copy -metadata major_brand= " "-c copy -t 0.2" "-show_entries format_tags"

FATE_CAF_FFMPEG-$(call REMUX, CAF, AMR_DEMUXER AMR_PARSER) += fate-caf-amr_nb-remux
fate-caf-amr_nb-remux: CMD = transcode amr $(TARGET_SAMPLES)/amrnb/4.75k.amr caf "-c copy" "-c copy -t 0.2"

# Rounding each packet duration from a 999 Hz clock must not drop the last packet.
FATE_CAF_FFMPEG_FFPROBE-$(call REMUX, CAF, MOV_DEMUXER SETTS_BSF) += fate-caf-amr_wb-rounded-duration-remux
fate-caf-amr_wb-rounded-duration-remux: CMD = transcode mov $(TARGET_SAMPLES)/amrwb/seed-23k85.awb caf \
    "-c copy -bsf:a setts=time_base=1/999:prescale=1:duration=NEXT_PTS-PTS -frames:a 1000" \
    "-c copy -frames:a 2" "-count_packets -show_entries stream=duration_ts,nb_read_packets -of compact" \
    "" "" "-stream_loop 1"

# Two complete coded packets with 7 samples of priming and 11 of end discard.
FATE_CAF_FFMPEG_FFPROBE-$(call REMUX, CAF, MOV_DEMUXER TRIM_BSF) += fate-caf-amr_wb-skip-samples-remux
fate-caf-amr_wb-skip-samples-remux: CMD = transcode mov $(TARGET_SAMPLES)/amrwb/seed-23k85.awb caf \
    "-c copy -bsf:a trim=start=7:end=629" "-c copy" "-show_entries stream=initial_padding,duration_ts -of compact"

FATE_CAF_FFMPEG-$(call REMUX, CAF, MOV_DEMUXER) += fate-caf-qdm2-remux
fate-caf-qdm2-remux: CMD = transcode mov $(TARGET_SAMPLES)/qt-surge-suite/surge-2-16-B-QDM2.mov caf "-c copy" "-c copy -t 0.2"

FATE_CAF_FFMPEG-$(call REMUX, CAF, WAV_DEMUXER) += fate-caf-pcm_s24le-remux
fate-caf-pcm_s24le-remux: CMD = transcode wav $(TARGET_SAMPLES)/audio-reference/divertimenti_2ch_96kHz_s24.wav caf "-c copy" "-c copy -t 0.05"

FATE_CAF_FFMPEG-$(call REMUX, CAF, WAV_DEMUXER PCM_S24LE_DECODER \
                                   PCM_S24BE_ENCODER)            \
                              += fate-caf-pcm_s24-remux
fate-caf-pcm_s24-remux: CMD = transcode wav $(TARGET_SAMPLES)/audio-reference/divertimenti_2ch_96kHz_s24.wav caf "-c pcm_s24be" "-c copy -t 0.05"

FATE_CAF_FFMPEG-$(call REMUX, CAF, MOV_DEMUXER) += fate-caf-mace6-remux
fate-caf-mace6-remux: CMD = transcode mov $(TARGET_SAMPLES)/qtrle/Animation-16Greys.mov caf "-map 0:a -c copy" "-c copy -t 0.003"

# priming beyond the 16-bit OpusHead pre-skip
FATE_CAF_FFPROBE-$(call DEMDEC, CAF, OPUS) += fate-caf-opus-large-priming
fate-caf-opus-large-priming: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_data -show_entries stream=initial_padding,extradata $(TARGET_SAMPLES)/caf/opus-large-priming.caf

FATE_SAMPLES_FFMPEG         += $(FATE_CAF_FFMPEG-yes)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_CAF_FFMPEG_FFPROBE-yes)
FATE_SAMPLES_FFPROBE        += $(FATE_CAF_FFPROBE-yes)
fate-caf: $(FATE_CAF_FFMPEG-yes) $(FATE_CAF_FFMPEG_FFPROBE-yes) $(FATE_CAF_FFPROBE-yes)
