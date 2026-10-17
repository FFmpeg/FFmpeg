# Codecs that need samples discarded: the priming at the start of a stream,
# and the pre-roll before the target of a seek.
#
# For each sample: the priming and pre-roll declared for the audio stream,
# the first packets read after a seek, and the timing of the decoded output
# after a seek, with and without the samples before the target. The output
# is silenced, as only its timing is compared and the decoders are not
# bit-exact across platforms.

PREROLL_SEEK_TS = 0.53

# 1: test name, 2: demuxer, 3: decoder, 4: sample
define FATE_PREROLL_TEST
FATE_PREROLL_FFPROBE-$(call DEMDEC, $(2), $(3)) += fate-preroll-$(1)-declared fate-preroll-$(1)-landing
fate-preroll-$(1)-declared: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -select_streams a:0 -show_entries stream=codec_name,sample_rate,initial_padding,seek_preroll -print_format compact $(TARGET_SAMPLES)/$(4)
fate-preroll-$(1)-landing: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -select_streams a:0 -read_intervals $(PREROLL_SEEK_TS)%+\#3 -show_entries packet=pts,dts,duration -print_format compact $(TARGET_SAMPLES)/$(4)
FATE_PREROLL_FFMPEG-$(call FRAMECRC, $(2), $(3), PCM_S16LE_ENCODER ARESAMPLE_FILTER VOLUME_FILTER) += fate-preroll-$(1)-decode
fate-preroll-$(1)-decode: CMD = framecrc -ss $(PREROLL_SEEK_TS) -t 0.15 -i $(TARGET_SAMPLES)/$(4) -map 0:a:0 -af volume=0,aresample -c:a pcm_s16le
FATE_PREROLL_FFMPEG-$(call FRAMECRC, $(2), $(3), PCM_S16LE_ENCODER ARESAMPLE_FILTER VOLUME_FILTER) += fate-preroll-$(1)-decode-noaccurate
fate-preroll-$(1)-decode-noaccurate: CMD = framecrc -noaccurate_seek -ss $(PREROLL_SEEK_TS) -t 0.15 -i $(TARGET_SAMPLES)/$(4) -map 0:a:0 -af volume=0,aresample -c:a pcm_s16le
endef

$(eval $(call FATE_PREROLL_TEST,aac-mov,MOV,AAC,gapless/102400samples_qt-lc-aac.m4a))
$(eval $(call FATE_PREROLL_TEST,aac-caf,CAF,AAC,caf/aac.caf))
$(eval $(call FATE_PREROLL_TEST,aac-adts,AAC,AAC,aac/id3v2.aac))
$(eval $(call FATE_PREROLL_TEST,aac-flv,FLV,AAC,h264/test-4867.flv))
$(eval $(call FATE_PREROLL_TEST,aac-mkv,MATROSKA,AAC,mkv/test7_cut.mkv))
$(eval $(call FATE_PREROLL_TEST,aac-he-adts,AAC,AAC,audiomatch/tones_afconvert_44100_stereo_aac_he.adts))
$(eval $(call FATE_PREROLL_TEST,aac-he-implicit-mov,MOV,AAC,aac/CT_DecoderCheck/sbr_i-ps_i.mp4))
$(eval $(call FATE_PREROLL_TEST,opus-ogg,OGG,OPUS,audiomatch/tones_opus_48000_stereo.opus))
$(eval $(call FATE_PREROLL_TEST,opus-mkv,MATROSKA,OPUS,mkv/codec_delay_opus.mkv))
$(eval $(call FATE_PREROLL_TEST,opus-caf,CAF,OPUS,caf/opus.caf))
$(eval $(call FATE_PREROLL_TEST,opus-mpegts,MPEGTS,OPUS,opus/test-8-7.1.opus-small.ts))
$(eval $(call FATE_PREROLL_TEST,mp3,MP3,MP3FLOAT,gapless/gapless.mp3))
$(eval $(call FATE_PREROLL_TEST,mp3-itunes,MP3,MP3FLOAT,gapless/gapless-itunes.mp3))

# A seek that lands inside the priming, which here spans the first three
# packets: the rest of the priming, or the pre-roll if longer, is skipped.
FATE_PREROLL_FFMPEG-$(call FRAMECRC, CAF, AAC, PCM_S16LE_ENCODER ARESAMPLE_FILTER VOLUME_FILTER) += fate-preroll-aac-caf-decode-in-priming
fate-preroll-aac-caf-decode-in-priming: CMD = framecrc -noaccurate_seek -ss 0.01 -t 0.1 -i $(TARGET_SAMPLES)/caf/aac.caf -af volume=0,aresample -c:a pcm_s16le

# A forward seek, as ffplay does: the range starts before the target and is
# open at the end.
FATE_PREROLL_SEEK-$(call DEMDEC, MOV, AAC) += fate-preroll-aac-mov-seek-forward
fate-preroll-aac-mov-seek-forward: CMD = run libavformat/tests/seek$(EXESUF) $(TARGET_SAMPLES)/gapless/102400samples_qt-lc-aac.m4a -seekforw 530000 -seekmin 400000 -frames 2 -duration 3

# A seek past the end lands on the last packet, which carries the padding
# to discard at the end of the stream.
FATE_PREROLL_FFPROBE-$(call DEMDEC, CAF, AAC) += fate-preroll-aac-caf-landing-end
fate-preroll-aac-caf-landing-end: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -select_streams a:0 -read_intervals 5.45%+\#2 -show_entries packet=pts,duration:packet_side_data=skip_samples,discard_padding -print_format compact $(TARGET_SAMPLES)/caf/aac.caf
FATE_PREROLL_FFPROBE-$(call DEMDEC, MATROSKA, OPUS) += fate-preroll-opus-mkv-landing-end
fate-preroll-opus-mkv-landing-end: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -select_streams a:0 -read_intervals 1.1%+\#2 -show_entries packet=pts,duration:packet_side_data=skip_samples,discard_padding -print_format compact $(TARGET_SAMPLES)/mkv/codec_delay_opus.mkv

# A seek on the video stream, just after a keyframe: the audio pre-roll
# still needs room.
FATE_PREROLL_SEEK-$(call DEMDEC, MOV, AAC H264) += fate-preroll-aac-mov-seek-video
fate-preroll-aac-mov-seek-video: CMD = run libavformat/tests/seek$(EXESUF) $(TARGET_SAMPLES)/h264/twofields_packet.mp4 -stream_id 0 -seekback 182000 -frames 4 -duration 3

# A seek on the stream that starts later, close enough to its start that
# the seek targets it: the other stream is already past its own start.
FATE_PREROLL_SEEK-$(call ALLYES, MOV_DEMUXER MOV_MUXER AAC_DECODER FILE_PROTOCOL) += fate-preroll-aac-mov-seek-staggered
fate-preroll-aac-mov-seek-staggered: CMD = seek_staggered $(TARGET_SAMPLES)/gapless/102400samples_qt-lc-aac.m4a 1 -stream_id 1 -seekback 42500 -frames 4 -duration 3

# A seek through the concat demuxer, which seeks in the file it reads.
FATE_PREROLL_SEEK-$(call ALLYES, CONCAT_DEMUXER MOV_DEMUXER AAC_DECODER FILE_PROTOCOL) += fate-preroll-aac-concat-seek
fate-preroll-aac-concat-seek: CMD = seek_concat $(SRC_PATH)/tests/preroll.ffconcat $(TARGET_SAMPLES)/gapless/102400samples_qt-lc-aac.m4a -seekback 530000 -frames 3 -duration 3

# A seek close to the start of a stream whose start time is unknown.
FATE_PREROLL_SEEK-$(call DEMDEC, AAC, AAC) += fate-preroll-aac-adts-seek-start
fate-preroll-aac-adts-seek-start: CMD = run libavformat/tests/seek$(EXESUF) $(TARGET_SAMPLES)/aac/id3v2.aac -seekback 10000 -frames 2 -duration 3

$(FATE_PREROLL_SEEK-yes): libavformat/tests/seek$(EXESUF)

FATE_SAMPLES_FFPROBE += $(FATE_PREROLL_FFPROBE-yes)
FATE_SAMPLES_AVCONV  += $(FATE_PREROLL_SEEK-yes)
FATE_SAMPLES_FFMPEG  += $(FATE_PREROLL_FFMPEG-yes)

fate-preroll: $(FATE_PREROLL_FFPROBE-yes) $(FATE_PREROLL_FFMPEG-yes) $(FATE_PREROLL_SEEK-yes)
